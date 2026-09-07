from fastapi import FastAPI, APIRouter, HTTPException, Request
from fastapi.responses import FileResponse, Response
from dotenv import load_dotenv
from starlette.middleware.cors import CORSMiddleware
from motor.motor_asyncio import AsyncIOMotorClient
import os
import io
import re
import hashlib
import logging
from pathlib import Path
from pydantic import BaseModel, Field, ConfigDict
from typing import List, Optional
import uuid
from datetime import datetime, timezone

from PIL import Image


ROOT_DIR = Path(__file__).parent
load_dotenv(ROOT_DIR / '.env')

# --- TriCord Presence installer (.cia) served for FBI QR remote install ------
FILES_DIR = ROOT_DIR / "files"
CIA_FILENAME = "tricord-presence-installer.cia"
CIA_PATH = FILES_DIR / CIA_FILENAME
# Title version declared in sysmodule/tricord_presenced.rsf (Version: 2)
INSTALLER_VERSION = "2"


def _compute_cia_meta():
    """Return metadata for the built .cia (size + sha256 + mtime), or None if missing."""
    if not CIA_PATH.exists():
        return None
    st = CIA_PATH.stat()
    h = hashlib.sha256()
    size = 0
    with open(CIA_PATH, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
            size += len(chunk)
    return {"size": size, "sha256": h.hexdigest(), "mtime": st.st_mtime}


_CIA_META = _compute_cia_meta()


def _get_cia_meta():
    """Renvoie la meta CIA courante, en la recalculant si le fichier a été
    remplacé depuis le démarrage (build.sh + copie dans backend/files/)."""
    global _CIA_META
    if not CIA_PATH.exists():
        _CIA_META = None
        return None
    st = CIA_PATH.stat()
    if _CIA_META is None or _CIA_META.get("mtime") != st.st_mtime:
        _CIA_META = _compute_cia_meta()
    return _CIA_META

# MongoDB connection
mongo_url = os.environ['MONGO_URL']
client = AsyncIOMotorClient(mongo_url)
db = client[os.environ['DB_NAME']]

# Logger disponible dès le chargement du module (utilisé par les endpoints
# icônes/titres définis plus bas, avant la config finale ci-dessous).
logger = logging.getLogger(__name__)

# Create the main app without a prefix
app = FastAPI()

# Create a router with the /api prefix
api_router = APIRouter(prefix="/api")


# Define Models
class StatusCheck(BaseModel):
    model_config = ConfigDict(extra="ignore")  # Ignore MongoDB's _id field
    
    id: str = Field(default_factory=lambda: str(uuid.uuid4()))
    client_name: str
    timestamp: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))

class StatusCheckCreate(BaseModel):
    client_name: str

# Add your routes to the router instead of directly to app
@api_router.get("/")
async def root():
    return {"message": "Hello World"}


@api_router.get("/installer/info")
async def installer_info():
    """Metadata for the TriCord Presence .cia so the frontend can build the
    QR code (FBI remote install) and show file details."""
    meta = _get_cia_meta()
    if not meta:
        raise HTTPException(status_code=404, detail="installer .cia not built yet")
    return {
        "filename": CIA_FILENAME,
        "title": "TriCord Presence Installer",
        "version": INSTALLER_VERSION,
        "size": meta["size"],
        "sha256": meta["sha256"],
        # Relative path; the frontend prepends REACT_APP_BACKEND_URL to build
        # the absolute URL encoded in the QR code scanned by FBI.
        "download_path": f"/api/download/{CIA_FILENAME}",
    }


@api_router.api_route("/download/{filename}", methods=["GET", "HEAD"])
async def download_cia(filename: str):
    """Serve the built .cia as a raw binary download. FBI's 'Scan QR Code'
    remote-install downloads the file directly from this URL."""
    if filename != CIA_FILENAME:
        raise HTTPException(status_code=404, detail="unknown file")
    if not CIA_PATH.exists():
        raise HTTPException(status_code=404, detail="installer .cia not built yet")
    return FileResponse(
        path=str(CIA_PATH),
        media_type="application/octet-stream",
        filename=CIA_FILENAME,
        headers={"Cache-Control": "no-cache"},
    )


# --- TriCord Presence : cache titres + icônes SMDH (Phase C) --------------
# Le sysmodule 3DS peut POST l'icône SMDH brute (RGB565 48x48 = 4608 octets)
# du titre en cours de jeu ; on la convertit en PNG et on l'expose via GET,
# afin de la référencer depuis la Rich Presence Discord (assets.large_image
# via `mp:external/...`). Le PNG est aussi utile pour le frontend web.
ICONS_DIR = FILES_DIR / "icons"
ICONS_DIR.mkdir(parents=True, exist_ok=True)

TID_RE = re.compile(r"^[0-9a-fA-F]{16}$")
SMDH_LARGE_ICON_BYTES = 48 * 48 * 2  # 4608, RGB565 big-icon SMDH slot
# Ordre "Z" (tuiles 8x8 sous-divisées en 4x4/2x2/1x1) utilisé par les SMDH 3DS.
# Table (i -> (dx, dy)) pour un bloc 8x8, calculée une fois.
_TILE_ORDER = None


def _make_tile_order():
    order = []
    # 8x8 tile decomposed as Morton/Z-order (as documented on 3dbrew "SMDH").
    for i in range(64):
        # bit interleave: x=bits 0,2,4  y=bits 1,3,5
        x = ((i >> 0) & 1) | (((i >> 2) & 1) << 1) | (((i >> 4) & 1) << 2)
        y = ((i >> 1) & 1) | (((i >> 3) & 1) << 1) | (((i >> 5) & 1) << 2)
        order.append((x, y))
    return order


_TILE_ORDER = _make_tile_order()


def _rgb565_smdh_to_png(rgb565: bytes) -> bytes:
    """Convertit le bloc icône SMDH large (48x48, RGB565 en ordre Z 8x8) en PNG.
    Le format SMDH range les pixels par tuiles 8x8 en ordre Morton, cf.
    3dbrew.org/wiki/SMDH#Icon_Graphics.
    """
    if len(rgb565) != SMDH_LARGE_ICON_BYTES:
        raise ValueError(f"expected {SMDH_LARGE_ICON_BYTES} bytes RGB565, got {len(rgb565)}")
    img = Image.new("RGB", (48, 48))
    px = img.load()
    idx = 0
    for tile_y in range(0, 48, 8):
        for tile_x in range(0, 48, 8):
            for (dx, dy) in _TILE_ORDER:
                lo = rgb565[idx]
                hi = rgb565[idx + 1]
                idx += 2
                v = lo | (hi << 8)
                r = ((v >> 11) & 0x1F) * 255 // 31
                g = ((v >> 5) & 0x3F) * 255 // 63
                b = (v & 0x1F) * 255 // 31
                px[tile_x + dx, tile_y + dy] = (r, g, b)
    # Upscale x4 => 192x192 (Discord préfère >= 128px pour rendre net l'icône).
    img = img.resize((192, 192), Image.NEAREST)
    buf = io.BytesIO()
    img.save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def _normalize_tid(tid: str) -> str:
    tid = tid.strip().upper()
    if tid.endswith(".PNG"):
        tid = tid[:-4]
    if not TID_RE.match(tid):
        raise HTTPException(status_code=400, detail="Title ID must be 16 hex chars")
    return tid


@api_router.post("/icons/{tid}")
async def upload_icon(tid: str, request: Request):
    """Reçoit l'icône SMDH brute (4608 octets RGB565 48x48 en ordre Z SMDH)
    depuis le sysmodule 3DS, la convertit en PNG et la stocke."""
    tid = _normalize_tid(tid)
    body = await request.body()
    if len(body) != SMDH_LARGE_ICON_BYTES:
        raise HTTPException(
            status_code=400,
            detail=f"Expected {SMDH_LARGE_ICON_BYTES} bytes RGB565, got {len(body)}",
        )
    try:
        png = _rgb565_smdh_to_png(body)
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"decode error: {e}")
    out = ICONS_DIR / f"{tid}.png"
    out.write_bytes(png)
    # Index MongoDB (best-effort, ne bloque pas si Mongo est down).
    icon_url = f"/api/icons/{tid}.png"
    try:
        await db.titles.update_one(
            {"tid": tid},
            {"$set": {
                "tid": tid,
                "icon_url": icon_url,
                "icon_sha256": hashlib.sha256(png).hexdigest(),
                "updated_at": datetime.now(timezone.utc).isoformat(),
            }},
            upsert=True,
        )
    except Exception as e:
        logger.warning("Mongo update for icon %s failed: %s", tid, e)
    return {"tid": tid, "icon_url": icon_url, "size": len(png)}


@api_router.api_route("/icons/{tid}.png", methods=["GET", "HEAD"])
async def get_icon(tid: str):
    tid = _normalize_tid(tid + ".png")
    out = ICONS_DIR / f"{tid}.png"
    if not out.exists():
        raise HTTPException(status_code=404, detail="icon not uploaded yet")
    return FileResponse(
        path=str(out),
        media_type="image/png",
        headers={"Cache-Control": "public, max-age=86400"},
    )


class TitleInfo(BaseModel):
    model_config = ConfigDict(extra="ignore")
    tid: str
    name: Optional[str] = None
    icon_url: Optional[str] = None
    source: Optional[str] = None  # "smdh" | "3dsdb" | "manual"
    updated_at: Optional[str] = None


class TitleUpsert(BaseModel):
    name: Optional[str] = None
    source: Optional[str] = "smdh"


@api_router.get("/titles/{tid}")
async def get_title(tid: str):
    """Renvoie les métadonnées connues pour un TID (nom + URL d'icône)."""
    tid = _normalize_tid(tid)
    doc = None
    try:
        doc = await db.titles.find_one({"tid": tid}, {"_id": 0})
    except Exception as e:
        logger.warning("Mongo find for title %s failed: %s", tid, e)
    if not doc:
        # Si on n'a pas d'entrée en base, on renvoie au moins l'URL d'icône
        # potentielle (le sysmodule peut ainsi POST l'icône plus tard).
        return {"tid": tid, "name": None, "icon_url": None, "source": None}
    return doc


@api_router.post("/titles/{tid}")
async def upsert_title(tid: str, payload: TitleUpsert):
    tid = _normalize_tid(tid)
    doc = {
        "tid": tid,
        "name": payload.name,
        "source": payload.source or "manual",
        "updated_at": datetime.now(timezone.utc).isoformat(),
    }
    try:
        await db.titles.update_one({"tid": tid}, {"$set": doc}, upsert=True)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"mongo error: {e}")
    return doc

@api_router.post("/status", response_model=StatusCheck)
async def create_status_check(input: StatusCheckCreate):
    status_dict = input.model_dump()
    status_obj = StatusCheck(**status_dict)
    
    # Convert to dict and serialize datetime to ISO string for MongoDB
    doc = status_obj.model_dump()
    doc['timestamp'] = doc['timestamp'].isoformat()
    
    _ = await db.status_checks.insert_one(doc)
    return status_obj

@api_router.get("/status", response_model=List[StatusCheck])
async def get_status_checks():
    # Exclude MongoDB's _id field from the query results
    status_checks = await db.status_checks.find({}, {"_id": 0}).to_list(1000)
    
    # Convert ISO string timestamps back to datetime objects
    for check in status_checks:
        if isinstance(check['timestamp'], str):
            check['timestamp'] = datetime.fromisoformat(check['timestamp'])
    
    return status_checks

# Include the router in the main app
app.include_router(api_router)

app.add_middleware(
    CORSMiddleware,
    allow_credentials=True,
    allow_origins=os.environ.get('CORS_ORIGINS', '*').split(','),
    allow_methods=["*"],
    allow_headers=["*"],
)

# Configure logging (idempotent, logger déjà créé plus haut).
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)

@app.on_event("shutdown")
async def shutdown_db_client():
    client.close()