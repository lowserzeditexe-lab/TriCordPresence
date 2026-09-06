#include "title_db.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Base Title ID -> nom, chargée depuis sdmc:/3ds/tricord-presence/titles.txt
 * (copié par l'installeur). Le fichier est généré au build par
 * tools/gen_titles_db.py depuis 3dsdb (hax0kartik/3dsdb, la base dont
 * 3DS-RPC utilise une version modifiée) : une ligne par titre,
 * "<TitleID 16 hex majuscules>\t<nom>\n", trié par Title ID croissant.
 *
 * Pourquoi un fichier SD et pas un romfs : un sysmodule CXI n'a pas de romfs
 * monté automatiquement (pas d'APT/romfsInit), et un fichier texte reste
 * éditable par l'utilisateur pour ajouter un titre manquant.
 */

#define TITLE_DB_PATH "sdmc:/3ds/tricord-presence/titles.txt"

typedef struct {
    u64 tid;
    u32 nameOff; // offset dans s_names
} title_entry_t;

static title_entry_t *s_entries = NULL;
static size_t s_count = 0;
static char *s_names = NULL;

void titleDbInit(void) {
    FILE *f = fopen(TITLE_DB_PATH, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 4 * 1024 * 1024) { fclose(f); return; }

    s_names = (char *)malloc((size_t)size + 1);
    if (!s_names || fread(s_names, 1, (size_t)size, f) != (size_t)size) {
        free(s_names); s_names = NULL; fclose(f); return;
    }
    fclose(f);
    s_names[size] = '\0';

    size_t lines = 0;
    for (long i = 0; i < size; i++) if (s_names[i] == '\n') lines++;
    s_entries = (title_entry_t *)malloc((lines + 1) * sizeof(title_entry_t));
    if (!s_entries) { free(s_names); s_names = NULL; return; }

    char *p = s_names;
    while (*p) {
        char *eol = strchr(p, '\n');
        if (!eol) eol = p + strlen(p);
        char *tab = memchr(p, '\t', (size_t)(eol - p));
        if (tab && tab - p == 16) {
            *tab = '\0';
            if (*eol) { if (eol > p && eol[-1] == '\r') eol[-1] = '\0'; *eol = '\0'; eol++; }
            s_entries[s_count].tid = strtoull(p, NULL, 16);
            s_entries[s_count].nameOff = (u32)(tab + 1 - s_names);
            s_count++;
        } else {
            if (*eol) eol++;
        }
        p = eol;
    }
}

void titleDbLookup(u64 titleId, char *outName, size_t outSize) {
    size_t lo = 0, hi = s_count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (s_entries[mid].tid < titleId) lo = mid + 1;
        else if (s_entries[mid].tid > titleId) hi = mid;
        else { snprintf(outName, outSize, "%s", s_names + s_entries[mid].nameOff); return; }
    }
    // Fallback lisible si le titre est absent de la base
    snprintf(outName, outSize, "Title %016llX", (unsigned long long)titleId);
}

void titleDbExit(void) {
    free(s_entries); s_entries = NULL;
    free(s_names);   s_names = NULL;
    s_count = 0;
}
