#include "ipc_server.h"
#include <3ds.h>
#include <string.h>
#include "log.h"

/*
 * Serveur IPC sur un port global nommé (svcCreatePort avec nom), comme
 * "hb:ldr"/"err:f" de Rosalina : aucun enregistrement srv: nécessaire côté
 * serveur, et côté client (le plugin, injecté dans le process du jeu) un
 * simple svcConnectToPort suffit — pas de droit de service exheader requis,
 * contrairement à srvGetServiceHandle. Cf Luma3DS
 * sysmodules/rosalina/source/service_manager.c (boucle svcReplyAndReceive
 * dont cette implémentation est une version réduite).
 *
 * Le traitement tourne dans un thread dédié : svcReplyAndReceive est
 * bloquant, il ne peut pas être "pollé" depuis la boucle principale.
 */

#define MAX_SESSIONS 4

static Handle s_port;                       // handle serveur du port
static Handle s_handles[1 + MAX_SESSIONS];  // [0] = port, puis sessions
static s32 s_numSessions = 0;
static Thread s_thread;
static volatile bool s_running = false;

static LightLock s_stateLock;
static presence_state_t s_lastState;

static void handleRequest(u32 *cmdbuf) {
    u16 cmd = (u16)(cmdbuf[0] >> 16);
    switch (cmd) {
    case PRESENCE_CMD_GET_STATE: {
        presence_state_t st;
        LightLock_Lock(&s_stateLock);
        st = s_lastState;
        LightLock_Unlock(&s_stateLock);
        cmdbuf[0] = IPC_MakeHeader(PRESENCE_CMD_GET_STATE, 1 + PRESENCE_IPC_STATE_WORDS, 0);
        cmdbuf[1] = 0;
        presenceStatePack(&st, &cmdbuf[2]);
        break;
    }
    default:
        // Même réponse "commande invalide" que Rosalina (0xD900182F).
        cmdbuf[0] = IPC_MakeHeader(0, 1, 0);
        cmdbuf[1] = 0xD900182F;
        break;
    }
}

static void ipcThreadMain(void *arg) {
    (void)arg;
    u32 *cmdbuf = getThreadCommandBuffer();
    Handle replyTarget = 0;
    s32 index;

    while (s_running) {
        if (replyTarget == 0) cmdbuf[0] = 0xFFFF0000; // "pas de réponse", juste recevoir

        index = -1;
        Result rc = svcReplyAndReceive(&index, s_handles, 1 + s_numSessions, replyTarget);

        if (rc == (Result)0xC920181A) { // session fermée par le client
            s32 off;
            if (index == -1) {
                for (off = 0; off < s_numSessions && s_handles[1 + off] != replyTarget; off++);
                if (off >= s_numSessions) { replyTarget = 0; continue; }
                index = 1 + off;
            }
            if (index >= 1) {
                svcCloseHandle(s_handles[index]);
                s_handles[index] = s_handles[1 + --s_numSessions];
            }
            replyTarget = 0;
            continue;
        }
        if (R_FAILED(rc)) {
            logPrintf("ipc: svcReplyAndReceive rc=%08lX", (unsigned long)rc);
            replyTarget = 0;
            svcSleepThread(100 * 1000000ULL);
            continue;
        }

        replyTarget = 0;
        if (index == 0) {
            // Nouvelle connexion sur le port
            Handle session;
            if (R_SUCCEEDED(svcAcceptSession(&session, s_port))) {
                if (s_numSessions < MAX_SESSIONS) s_handles[1 + s_numSessions++] = session;
                else svcCloseHandle(session);
            }
        } else {
            handleRequest(cmdbuf);
            replyTarget = s_handles[index];
        }
    }
}

Result ipcServerInit(const char *portName) {
    memset(&s_lastState, 0, sizeof(s_lastState));
    LightLock_Init(&s_stateLock);

    // Un seul jeu actif à la fois -> 1 plugin client, marge à MAX_SESSIONS
    // pour un reconnect rapide après changement de jeu.
    Handle clientPort;
    Result rc = svcCreatePort(&s_port, &clientPort, portName, MAX_SESSIONS);
    if (R_FAILED(rc)) return rc;
    svcCloseHandle(clientPort);

    s_handles[0] = s_port;
    s_numSessions = 0;
    s_running = true;
    s_thread = threadCreate(ipcThreadMain, NULL, 0x2000, 0x3F, -2, false);
    if (!s_thread) {
        s_running = false;
        svcCloseHandle(s_port);
        return MAKERESULT(RL_PERMANENT, RS_OUTOFRESOURCE, RM_APPLICATION, RD_OUT_OF_MEMORY);
    }
    return 0;
}

void ipcServerPoll(void) {
    // Rien à faire : le thread IPC est bloquant et autonome. Conservé pour
    // garder l'API du squelette (appelé depuis la boucle principale).
}

void ipcServerBroadcastState(const presence_state_t *state) {
    // Modèle "pull" : le plugin interroge à intervalle régulier, on ne fait
    // que publier l'état partagé (pas de push vers le client).
    LightLock_Lock(&s_stateLock);
    s_lastState = *state;
    LightLock_Unlock(&s_stateLock);
}

void ipcServerExit(void) {
    s_running = false;
    svcCloseHandle(s_port); // débloque svcReplyAndReceive
    if (s_thread) { threadJoin(s_thread, U64_MAX); threadFree(s_thread); }
    for (s32 i = 0; i < s_numSessions; i++) svcCloseHandle(s_handles[1 + i]);
    s_numSessions = 0;
}
