#!/usr/bin/env python3
"""Génère titles.txt (Title ID -> nom) depuis 3dsdb (hax0kartik/3dsdb), la
base dont 3DS-RPC utilise une version modifiée.

Format de sortie (lu par sysmodule/source/title_db.c) :
    <TitleID 16 hex MAJ>\t<nom>\n   trié par Title ID croissant, dédoublonné.

Usage : gen_titles_db.py <sortie> [--offline <dossier de jsons déjà téléchargés>]
"""
import json, os, sys, urllib.request

REGIONS = ["GB", "US", "JP", "KR", "TW"]   # ordre de priorité des noms (anglais d'abord)
BASE = "https://raw.githubusercontent.com/hax0kartik/3dsdb/master/jsons/list_{}.json"

def load(region, cache_dir):
    path = os.path.join(cache_dir, f"list_{region}.json")
    if not os.path.exists(path):
        os.makedirs(cache_dir, exist_ok=True)
        with urllib.request.urlopen(BASE.format(region), timeout=60) as r:
            open(path, "wb").write(r.read())
    return json.load(open(path, encoding="utf-8"))

def clean(name):
    for sym in ("\u2122", "\u00ae", "\u00a9"):  # ™ ® © inutiles pour l'affichage
        name = name.replace(sym, "")
    name = " ".join(name.replace("\n", " ").replace("\t", " ").split())
    return name.encode("utf-8")[:63].decode("utf-8", "ignore")  # game_name[64] côté C

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "titles.txt"
    cache = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == "--offline" else os.path.join(os.path.dirname(out) or ".", ".3dsdb_cache")
    titles = {}
    for region in REGIONS:
        try:
            entries = load(region, cache)
        except Exception as e:
            print(f"!! {region}: {e}", file=sys.stderr)
            continue
        for e in entries:
            tid = (e.get("TitleID") or "").strip().upper()
            name = clean(e.get("Name") or "")
            if len(tid) != 16 or not name:
                continue
            # Seules les applications (00040000) et démos (00040002) sont
            # des "jeux" au premier plan ; on ignore DLC/mises à jour.
            if tid[:8] not in ("00040000", "00040002"):
                continue
            titles.setdefault(tid, name)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        for tid in sorted(titles):
            f.write(f"{tid}\t{titles[tid]}\n")
    print(f"{len(titles)} titres -> {out}")

if __name__ == "__main__":
    main()
