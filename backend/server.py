from fastapi import FastAPI, APIRouter, HTTPException
from fastapi.responses import FileResponse
from dotenv import load_dotenv
from starlette.middleware.cors import CORSMiddleware
from motor.motor_asyncio import AsyncIOMotorClient
import os
import hashlib
import logging
from pathlib import Path
from pydantic import BaseModel, Field, ConfigDict
from typing import List
import uuid
from datetime import datetime, timezone


ROOT_DIR = Path(__file__).parent
load_dotenv(ROOT_DIR / '.env')

# --- TriCord Presence installer (.cia) served for FBI QR remote install ------
FILES_DIR = ROOT_DIR / "files"
CIA_FILENAME = "tricord-presence-installer.cia"
CIA_PATH = FILES_DIR / CIA_FILENAME
# Title version declared in sysmodule/tricord_presenced.rsf (Version: 2)
INSTALLER_VERSION = "2"


def _compute_cia_meta():
    """Return metadata for the built .cia (size + sha256), or None if missing."""
    if not CIA_PATH.exists():
        return None
    h = hashlib.sha256()
    size = 0
    with open(CIA_PATH, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
            size += len(chunk)
    return {"size": size, "sha256": h.hexdigest()}


_CIA_META = _compute_cia_meta()

# MongoDB connection
mongo_url = os.environ['MONGO_URL']
client = AsyncIOMotorClient(mongo_url)
db = client[os.environ['DB_NAME']]

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
    meta = _CIA_META or _compute_cia_meta()
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

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

@app.on_event("shutdown")
async def shutdown_db_client():
    client.close()