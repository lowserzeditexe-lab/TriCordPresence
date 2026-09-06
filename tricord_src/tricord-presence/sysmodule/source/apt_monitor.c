#include "apt_monitor.h"
#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "title_db.h"

/*
 * Accès brut au service APT (hébergé par NS) depuis un sysmodule.
 *
 * Pourquoi pas aptInit() : il enregistre le process comme applet
 * (APT:Initialize / Enable) — inutile et bloquant pour un daemon.
 *
 * Pourquoi ouvrir/fermer la session à chaque appel : NS limite le nombre de
 * sessions APT simultanées ; libctru fait exactement ça dans aptSendCommand()
 * (libctru/source/services/apt.c : srvGetServiceHandle -> svcSendSyncRequest
 * -> svcCloseHandle), avec le même ordre d'essai "APT:S", "APT:A", "APT:U".
 * Les trois noms sont déclarés dans tricord_presenced.rsf.
 */
static const char *const s_aptServiceNames[3] = { "APT:S", "APT:A", "APT:U" };
static int s_aptServiceIdx = -1;

/*
 * Filtre : Title IDs à ne jamais publier, lus dans config.txt (lignes
 * "exclude=<TID hex>" répétables, ou "exclude=<TID>,<TID>"). Un jeu exclu
 * est remonté comme PRESENCE_KIND_IDLE (statut idle, pas de nom de jeu).
 */
#define CONFIG_PATH   "sdmc:/3ds/tricord-presence/config.txt"
#define MAX_EXCLUDES  64
static u64 s_excludes[MAX_EXCLUDES];
static int s_numExcludes = 0;

static void loadExcludes(void) {
    s_numExcludes = 0;
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "exclude=", 8) != 0) continue;
        char *p = line + 8;
        while (*p && s_numExcludes < MAX_EXCLUDES) {
            while (*p == ' ' || *p == ',') p++;
            char *end;
            u64 tid = strtoull(p, &end, 16);
            if (end == p) break;
            if (tid) s_excludes[s_numExcludes++] = tid;
            p = end;
        }
    }
    fclose(f);
}

static bool isExcluded(u64 tid) {
    for (int i = 0; i < s_numExcludes; i++)
        if (s_excludes[i] == tid) return true;
    return false;
}

Result aptMonitorInit(void) {
    loadExcludes();
    Handle h;
    for (int i = 0; i < 3; i++) {
        if (R_SUCCEEDED(srvGetServiceHandle(&h, s_aptServiceNames[i]))) {
            svcCloseHandle(h);
            s_aptServiceIdx = i;
            return 0;
        }
    }
    return MAKERESULT(RL_PERMANENT, RS_NOTFOUND, RM_APPLICATION, RD_NOT_FOUND);
}

static Result aptSendRaw(u32 *cmdbuf_in_out) {
    if (s_aptServiceIdx < 0) return -1;
    Handle h;
    Result rc = srvGetServiceHandle(&h, s_aptServiceNames[s_aptServiceIdx]);
    if (R_FAILED(rc)) return rc;

    u32 *cmdbuf = getThreadCommandBuffer();
    memcpy(cmdbuf, cmdbuf_in_out, 4 * 16);
    rc = svcSendSyncRequest(h);
    if (R_SUCCEEDED(rc)) {
        memcpy(cmdbuf_in_out, cmdbuf, 4 * 16);
        rc = (Result)cmdbuf[1];
    }
    svcCloseHandle(h);
    return rc;
}

/*
 * APT:GetAppletManInfo — https://www.3dbrew.org/wiki/APT:GetAppletManInfo
 *   Requête : [0] 0x00050040  [1] AppletPos (entrée, -1 = aucune)
 *   Réponse : [1] result [2] AppletPos [3] Requested AppID
 *             [4] HOME Menu AppID [5] Current (actif) AppID
 * (le squelette utilisait 0x0001 = APT:GetLockHandle, corrigé.)
 */
static Result aptGetAppletManInfo(u8 *outAppletPos, u32 *outMenuAppId, u32 *outActiveAppId) {
    u32 cmdbuf[16] = {0};
    cmdbuf[0] = IPC_MakeHeader(0x0005, 1, 0);
    cmdbuf[1] = (u32)APTPOS_NONE;

    Result rc = aptSendRaw(cmdbuf);
    if (R_FAILED(rc)) return rc;
    if (outAppletPos)   *outAppletPos   = (u8)cmdbuf[2];
    if (outMenuAppId)   *outMenuAppId   = cmdbuf[4];
    if (outActiveAppId) *outActiveAppId = cmdbuf[5];
    return 0;
}

/*
 * APT:GetAppletInfo — https://www.3dbrew.org/wiki/APT:GetAppletInfo
 *   Requête : [0] 0x00060040  [1] AppID
 *   Réponse : [1] result [2-3] u64 TitleID [4] MediaType [5] u8 Registered
 *             [6] u8 Loaded [7] AppletAttr
 *   Erreur 0xC880CFFA si aucune app(let) n'est enregistrée pour cet AppID.
 * Signature identique à libctru APT_GetAppletInfo (services/apt.h).
 */
static Result aptGetAppletInfo(u32 appId, u64 *outTitleId, bool *outRegistered) {
    u32 cmdbuf[16] = {0};
    cmdbuf[0] = IPC_MakeHeader(0x0006, 1, 0);
    cmdbuf[1] = appId;

    Result rc = aptSendRaw(cmdbuf);
    if (R_FAILED(rc)) return rc;
    if (outTitleId)    *outTitleId    = ((u64)cmdbuf[3] << 32) | cmdbuf[2];
    if (outRegistered) *outRegistered = (cmdbuf[5] & 0xFF) != 0;
    return 0;
}

Result aptMonitorGetCurrentState(presence_state_t *out) {
    memset(out, 0, sizeof(*out));

    u8 pos = 0;
    u32 menuAppId = 0, activeAppId = 0;
    Result rc = aptGetAppletManInfo(&pos, &menuAppId, &activeAppId);
    if (R_FAILED(rc)) return rc;

    // AppIDs : libctru services/apt.h (NS_APPID) / 3dbrew NS_and_APT_Services#AppIDs
    //   0x101 = HOME Menu, 0x300 = Application, 0x4xx = applets bibliothèque
    //   (clavier, erreur...) qui passent au premier plan par-dessus le jeu.
    // Un applet système ou bibliothèque actif ne change pas le jeu "en cours" :
    // on regarde donc si une Application (0x300) est enregistrée, et on
    // considère "idle" uniquement quand rien ne tourne en slot Application.
    if (activeAppId == APPID_HOMEMENU || activeAppId == menuAppId) {
        u64 tid = 0;
        bool registered = false;
        rc = aptGetAppletInfo(APPID_APPLICATION, &tid, &registered);
        if (R_FAILED(rc) || !registered) {
            out->kind = PRESENCE_KIND_IDLE; // HOME menu seul, aucun jeu suspendu
            return 0;
        }
        // Un jeu est suspendu derrière le HOME menu : on le considère toujours
        // "en cours" (comportement Discord desktop : le jeu reste ouvert).
    }

    u64 titleId = 0;
    rc = aptMonitorGetActiveTitleId(&titleId);
    if (R_FAILED(rc)) {
        out->kind = PRESENCE_KIND_UNKNOWN;
        return rc;
    }

    if (isExcluded(titleId)) {
        out->kind = PRESENCE_KIND_IDLE; // jeu masqué par l'utilisateur (config.txt exclude=)
        return 0;
    }

    out->kind = PRESENCE_KIND_IN_GAME;
    out->title_id = titleId;
    titleDbLookup(titleId, out->game_name, sizeof(out->game_name));
    return 0;
}

// Title ID du jeu : l'Application au premier plan est toujours enregistrée
// auprès de NS sous l'AppID 0x300 (un seul slot Application sur 3DS), donc
// APT:GetAppletInfo(0x300) suffit — pas besoin d'AM ni de pm:app.
// TODO(hardware): non validé sur console. Alternative si NS refuse l'appel
// depuis un sysmodule : svcGetProcessList + svcGetProcessInfo(handle, 0x10001)
// (extension kernel Luma3DS, utilisée par rosalina/source/errdisp.c) qui
// nécessite en plus les SVC GetProcessList/OpenProcess dans le .rsf.
Result aptMonitorGetActiveTitleId(u64 *outTitleId) {
    u64 tid = 0;
    bool registered = false;
    Result rc = aptGetAppletInfo(APPID_APPLICATION, &tid, &registered);
    if (R_FAILED(rc)) return rc;
    if (!registered || tid == 0)
        return MAKERESULT(RL_STATUS, RS_NOTFOUND, RM_APPLICATION, RD_NOT_FOUND);
    *outTitleId = tid;
    return 0;
}

void aptMonitorExit(void) {
    s_aptServiceIdx = -1;
}
