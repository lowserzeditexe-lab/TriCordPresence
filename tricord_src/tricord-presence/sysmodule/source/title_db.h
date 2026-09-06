#pragma once
#include <3ds/types.h>
#include <stddef.h>

// Résout un Title ID en nom de jeu lisible.
// Source : sdmc:/3ds/tricord-presence/titles.txt (généré par
// tools/gen_titles_db.py depuis 3dsdb, copié par l'installeur), chargé une
// fois au démarrage en RAM (tableau trié + recherche dichotomique).
// Un fichier SD a été préféré à un romfs : pas de romfs pour un sysmodule
// CXI sans APT, et le fichier reste éditable par l'utilisateur.
void titleDbInit(void);
void titleDbLookup(u64 titleId, char *outName, size_t outSize);
void titleDbExit(void);
