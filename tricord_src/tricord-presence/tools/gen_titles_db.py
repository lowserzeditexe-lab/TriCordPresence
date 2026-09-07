#!/usr/bin/env python3
"""Génère titles.txt (Title ID -> nom) depuis 3dsdb (hax0kartik/3dsdb, la
base dont 3DS-RPC utilise une version modifiée) + un fichier d'entrées
manuelles optionnel (titles_extra.txt à côté du script, format identique).

Format de sortie (lu par sysmodule/source/title_db.c) :
    <TitleID 16 hex MAJ>\t<nom>\n   trié par Title ID croissant, dédoublonné.

Usage : gen_titles_db.py <sortie> [--offline <dossier de jsons déjà téléchargés>]
"""
import json, os, sys, urllib.request

REGIONS = ["GB", "US", "JP", "KR", "TW"]   # ordre de priorité des noms (anglais d'abord)
BASE = "https://raw.githubusercontent.com/hax0kartik/3dsdb/master/jsons/list_{}.json"
# Catégories 3DS retenues :
#   00040000 = jeux retail
#   00040002 = démos
#   00040010 = system applications (Health & Safety, Settings, HOME Menu, etc.)
# Sont EXCLUS (pas des "jeux au premier plan") : 00040008 (system data / DLC
# de mise à jour), 0004000E (patchs), 0004008C (DLC AddOnContent).
CATEGORIES = ("00040000", "00040002", "00040010")

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

def load_extras(out_path):
    """Fusionne un fichier titles_extra.txt éventuel situé à côté du script
    OU du fichier de sortie. Chaque ligne = "<TID>\t<nom>" (idem format
    3dsdb), les # sont ignorés. Permet à l'utilisateur d'ajouter homebrew,
    hacks, prototypes et autres TIDs absents de 3dsdb."""
    entries = {}
    candidates = [
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "titles_extra.txt"),
        os.path.join(os.path.dirname(out_path) or ".", "titles_extra.txt"),
    ]
    for path in candidates:
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as f:
            for line in f:
                s = line.strip()
                if not s or s.startswith("#"):
                    continue
                parts = s.split("\t", 1)
                if len(parts) != 2:
                    parts = s.split(None, 1)  # fallback: espace
                if len(parts) != 2:
                    continue
                tid = parts[0].strip().upper()
                if len(tid) != 16:
                    continue
                entries[tid] = clean(parts[1])
        print(f"+ {len(entries)} entrée(s) manuelles depuis {path}", file=sys.stderr)
    return entries

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
            if tid[:8] not in CATEGORIES:
                continue
            titles.setdefault(tid, name)
    # Les entrées manuelles écrasent les entrées 3dsdb (souvent plus précises).
    titles.update(load_extras(out))
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        for tid in sorted(titles):
            f.write(f"{tid}\t{titles[tid]}\n")
    print(f"{len(titles)} titres -> {out}")

if __name__ == "__main__":
    main()
