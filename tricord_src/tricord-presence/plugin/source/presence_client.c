#include "presence_client.h"
#include <3ds.h>
#include <string.h>

/*
 * Client IPC : se connecte au port global "presence:d" créé par le sysmodule
 * (svcCreatePort nommé). svcConnectToPort ne dépend d'aucune ACL de service
 * de l'exheader du jeu hôte — contrairement à srvGetServiceHandle — c'est
 * pourquoi le sysmodule expose un port global et non un service srv:
 * (même approche que le port "hb:ldr" de Luma3DS utilisé depuis n'importe
 * quel process 3dsx, et "plg:ldr" utilisé par CTRPF depuis le jeu).
 * SVC utilisés : ConnectToPort (0x2D), SendSyncRequest (0x32), CloseHandle
 * (0x23) — présents dans l'exheader de tout jeu (nécessaires à srv:).
 */

static Handle s_session = 0;
static bool s_connected = false;
static u32 s_retryCountdown = 0;

static void tryConnect(void) {
    Result rc = svcConnectToPort(&s_session, PRESENCE_IPC_PORT_NAME);
    s_connected = R_SUCCEEDED(rc);
    if (!s_connected) s_session = 0;
}

void presenceClientInit(void) {
    tryConnect();
    s_retryCountdown = 0;
}

bool presenceClientPoll(presence_state_t *out) {
    memset(out, 0, sizeof(*out));

    // Retry/backoff : le sysmodule peut ne pas être (encore) lancé quand le
    // jeu démarre — on retente toutes les 5 interrogations (~5 s).
    if (!s_connected) {
        if (s_retryCountdown > 0) { s_retryCountdown--; return false; }
        tryConnect();
        if (!s_connected) { s_retryCountdown = 5; return false; }
    }

    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(PRESENCE_CMD_GET_STATE, 0, 0);

    Result rc = svcSendSyncRequest(s_session);
    if (R_FAILED(rc)) {
        // Session invalidée (sysmodule redémarré / port fermé) : reconnecter
        svcCloseHandle(s_session);
        s_session = 0;
        s_connected = false;
        return false;
    }
    if (R_FAILED((Result)cmdbuf[1])) return false;
    if (cmdbuf[0] != IPC_MakeHeader(PRESENCE_CMD_GET_STATE, 1 + PRESENCE_IPC_STATE_WORDS, 0)) return false;

    presenceStateUnpack(out, &cmdbuf[2]);
    return true;
}

void presenceClientExit(void) {
    if (s_session) svcCloseHandle(s_session);
    s_session = 0;
    s_connected = false;
}
