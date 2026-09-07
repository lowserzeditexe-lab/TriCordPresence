#include "smdh_reader.h"
#include <3ds.h>
#include <string.h>
#include <stdio.h>

/*
 * Format SMDH (cf 3dbrew.org/wiki/SMDH) — offsets utiles :
 *   0x00        : magic "SMDH"
 *   0x08        : 16 x AppTitle (0x200 chacun)
 *                    0x00 shortDescription UTF-16-LE (0x80 = 64 chars)
 *                    0x80 longDescription  UTF-16-LE (0x100)
 *                    0x180 publisher       UTF-16-LE (0x80)
 *   0x2008      : icône petite 24x24 RGB565 (0x480 octets)
 *   0x24C0      : icône large 48x48 RGB565 (0x1200 = 4608 octets)
 *
 * Titles :
 *   0 = Japonais       1 = Anglais        2 = Français       3 = Allemand
 *   4 = Italien        5 = Espagnol       6 = Chinois simpl. 7 = Coréen
 *   8 = Néerlandais    9 = Portugais     10 = Russe          11 = Chinois trad.
 * On essaye EN d'abord (le plus universel pour un affichage Discord global),
 * puis FR, JA, DE, IT, ES, et enfin n'importe quel non-vide.
 */
#define SMDH_MAGIC        0x48444D53  /* "SMDH" en LE */
#define SMDH_APP_TITLE_OFF 0x08
#define SMDH_APP_TITLE_SZ  0x200
#define SMDH_ICON_LARGE_OFF 0x24C0

static const int NAME_LANG_ORDER[] = { 1, 2, 0, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

/* Décodage UTF-16-LE -> UTF-8, tronqué à outSize-1 octets et NUL-terminé.
 * On ne supporte que le BMP (pas de paires substitutives) — largement
 * suffisant pour des noms de jeux 3DS. */
static void utf16le_to_utf8(const u8 *in, size_t inBytes, char *out, size_t outSize) {
    size_t oi = 0;
    for (size_t i = 0; i + 1 < inBytes && oi + 1 < outSize; i += 2) {
        u16 cp = (u16)(in[i] | (in[i + 1] << 8));
        if (cp == 0) break;
        if (cp < 0x80) {
            if (oi + 1 >= outSize) break;
            out[oi++] = (char)cp;
        } else if (cp < 0x800) {
            if (oi + 2 >= outSize) break;
            out[oi++] = (char)(0xC0 | (cp >> 6));
            out[oi++] = (char)(0x80 | (cp & 0x3F));
        } else {
            if (oi + 3 >= outSize) break;
            out[oi++] = (char)(0xE0 | (cp >> 12));
            out[oi++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[oi++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[oi] = '\0';
}

/* Renvoie true si le nom pointé (UTF-16-LE, 0x80 octets max) est non vide. */
static bool smdhTitleNonEmpty(const u8 *title, size_t bytes) {
    for (size_t i = 0; i + 1 < bytes; i += 2) {
        u16 cp = (u16)(title[i] | (title[i + 1] << 8));
        if (cp == 0) break;
        if (cp != ' ' && cp != 0xA0) return true;
    }
    return false;
}

static void smdhPickBestName(const u8 *smdh, char *outShort, size_t shortSize,
                             char *outPublisher, size_t pubSize) {
    outShort[0] = '\0';
    outPublisher[0] = '\0';
    for (size_t k = 0; k < sizeof(NAME_LANG_ORDER) / sizeof(NAME_LANG_ORDER[0]); k++) {
        int lang = NAME_LANG_ORDER[k];
        const u8 *entry = smdh + SMDH_APP_TITLE_OFF + (size_t)lang * SMDH_APP_TITLE_SZ;
        const u8 *shortDesc = entry + 0x00;
        const u8 *publisher = entry + 0x180;
        if (!smdhTitleNonEmpty(shortDesc, 0x80)) continue;
        utf16le_to_utf8(shortDesc, 0x80, outShort, shortSize);
        utf16le_to_utf8(publisher, 0x80, outPublisher, pubSize);
        /* Beaucoup de noms 3DS ont un saut de ligne "Line1\nLine2" — on
         * remplace par " · " pour un affichage sur une ligne Discord. */
        for (char *p = outShort; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
        return;
    }
}

/* Ouvre l'archive SavedataAndContent (0x2345678a) pour <titleId>+<mediaType>
 * et lit le fichier "icon" (SMDH) via son chemin binaire ExeFS. */
static Result smdhReadOne(u32 mediaType, u64 titleId, u8 *out, size_t outSize, u32 *outRead) {
    /* Archive path binary : {tidLow, tidHigh, mediaType, 0x0} (4 x u32 = 16 octets).
     * Format issu de FlagBrew/Checkpoint/3ds/source/smdh.cpp — l'entier de
     * padding final est OBLIGATOIRE (sinon le kernel renvoie 0xC8804478
     * "invalid path"). */
    u32 archPath[4] = { (u32)(titleId & 0xFFFFFFFFULL), (u32)(titleId >> 32), mediaType, 0 };
    FS_Path aPath = { PATH_BINARY, sizeof(archPath), archPath };

    /* File path binary : {0, 0, 2 (type=ExeFS), 0x6E6F6369 ('icon' LE), 0}.
     * 0x2 = section ExeFS d'un NCCH, 0x6E6F6369 = "icon" en LE u32.
     * Utilisé par ftpd, ftpony, FBI, GodMode9, Checkpoint pour lire le SMDH. */
    u32 filePath[5] = { 0, 0, 2, 0x6E6F6369, 0 };
    FS_Path fPath = { PATH_BINARY, sizeof(filePath), filePath };

    Handle h = 0;
    Result rc = FSUSER_OpenFileDirectly(&h, ARCHIVE_SAVEDATA_AND_CONTENT, aPath, fPath, FS_OPEN_READ, 0);
    if (R_FAILED(rc)) return rc;

    u32 bytesRead = 0;
    rc = FSFILE_Read(h, &bytesRead, 0, out, (u32)outSize);
    FSFILE_Close(h);
    if (R_FAILED(rc)) return rc;
    if (outRead) *outRead = bytesRead;
    return 0;
}

/* Détermine le media type probable d'après la catégorie du TID.
 *   0x00040000 = jeu retail (souvent NAND si eShop, SD si téléchargé, GameCard sinon)
 *   0x00040002 = démo (NAND)
 *   0x00040010 = system app (NAND)
 *   0x0004000E = update (NAND)
 * Pour maximiser les chances de lire le SMDH d'un HOMEBREW installé via
 * FBI (généralement en SD, TID category 0x00040000 avec un unique-id
 * arbitraire), on essaie dans cet ordre : SD -> NAND -> Gamecard.
 * Chaque essai fait un OpenFileDirectly (ne monte pas l'archive de manière
 * persistante), donc coût minime.
 */
Result smdhReaderExtract(u64 titleId, smdh_info_t *out) {
    memset(out, 0, sizeof(*out));

    static u8 smdh[0x36C0];
    u32 bytesRead = 0;
    Result rc = -1;
    Result lastRc = 0;

    static const u32 tryTypes[] = { MEDIATYPE_SD, MEDIATYPE_NAND, MEDIATYPE_GAME_CARD };
    for (unsigned i = 0; i < sizeof(tryTypes) / sizeof(tryTypes[0]); i++) {
        rc = smdhReadOne(tryTypes[i], titleId, smdh, sizeof(smdh), &bytesRead);
        lastRc = rc;
        if (R_SUCCEEDED(rc) && bytesRead >= SMDH_ICON_LARGE_OFF + SMDH_ICON_LARGE_SIZE) {
            u32 magic = (u32)smdh[0] | ((u32)smdh[1] << 8) | ((u32)smdh[2] << 16) | ((u32)smdh[3] << 24);
            if (magic == SMDH_MAGIC) break;
            rc = -1;
        }
    }
    if (R_FAILED(rc)) return lastRc; /* renvoie la dernière erreur pour log */

    smdhPickBestName(smdh, out->name, sizeof(out->name), out->publisher, sizeof(out->publisher));
    memcpy(out->large_icon, smdh + SMDH_ICON_LARGE_OFF, SMDH_ICON_LARGE_SIZE);
    out->has_icon = true;
    return 0;
}
