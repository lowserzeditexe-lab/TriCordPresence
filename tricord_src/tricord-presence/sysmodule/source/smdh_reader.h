#pragma once
#include <3ds/types.h>
#include <3ds/result.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Lecture du SMDH d'un titre installé (NAND/SD) ou d'une cartouche.
 *
 * Un SMDH (System Menu Data Header, cf 3dbrew.org/wiki/SMDH) contient :
 *  - 16 noms localisés (JA/EN/FR/DE/IT/ES/ZH_CN/KO/NL/PT/RU/ZH_TW),
 *    chacun avec shortDesc (0x40 UTF-16-LE), longDesc (0x80) et publisher.
 *  - Une petite icône 24x24 RGB565 et une grande icône 48x48 RGB565.
 *
 * On accède au SMDH via l'archive "SavedataAndContent" (0x2345678a) +
 * chemin binaire ExeFS pour le fichier "icon" (u32[5]={0,0,2,'icon',0}).
 * Cette technique est utilisée par de nombreux homebrew (FBI, GodMode9,
 * ftpony...) et fonctionne pour tout titre installé.
 */

#define SMDH_ICON_LARGE_SIZE (48 * 48 * 2)  /* 4608 octets RGB565 en ordre Z */
#define SMDH_NAME_MAX 128                    /* nom UTF-8, largement suffisant */

typedef struct {
    /* Nom court localisé (UTF-8, NUL-terminé). */
    char name[SMDH_NAME_MAX];
    /* Nom de l'éditeur (UTF-8, NUL-terminé), utile en small_text. */
    char publisher[SMDH_NAME_MAX];
    /* Icône large 48x48 RGB565 telle qu'écrite dans le SMDH (ordre Z SMDH,
     * cf 3dbrew) — sera envoyée telle quelle au backend qui la convertit
     * en PNG. */
    u8   large_icon[SMDH_ICON_LARGE_SIZE];
    bool has_icon;
} smdh_info_t;

/*
 * Extrait le SMDH du titre <titleId> (media type déduit du bit "SD/NAND" du
 * TID, ou testé dans l'ordre NAND -> SD -> Gamecard). Renvoie 0 en succès.
 * Le SMDH n'existe pas pour tous les titres (démos anciennes, dev...) :
 * l'appelant doit gérer un échec silencieusement (fallback titles.txt).
 */
Result smdhReaderExtract(u64 titleId, smdh_info_t *out);
