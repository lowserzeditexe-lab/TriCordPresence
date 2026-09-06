/*
 * tricord_presenced — sysmodule résident
 *
 * Rôle : tourne en tâche de fond, surveille l'état de la console (menu HOME
 * vs jeu lancé) et pousse les mises à jour de présence sur la Gateway
 * Discord via une connexion websocket persistante (thread dédié, cf
 * discord_gateway.c). Expose l'état courant au plugin .3gx via le port
 * global "presence:d" (thread dédié, cf ipc_server.c).
 *
 * Chargement : Luma3DS >= 12 charge /luma/sysmodules/000401300F000102.cxi
 * quand PM lance ce Title ID (loader/source/loader.c, openSysmoduleCxi).
 * TODO(hardware): Luma3DS "mainline" ne lance PAS de lui-même un sysmodule
 * custom au boot : il faut soit le lancer via pm:app (ce que fait
 * l'installeur, méthode Plug-n-play), soit l'ajouter aux dépendances d'un
 * sysmodule démarré au boot via /luma/titles/<tid>/exheader.bin. Voir
 * RAPPORT.md §"Lancement au boot".
 */

#include <3ds.h>
#include <3ds/allocator/mappable.h>
#include <string.h>
#include "apt_monitor.h"
#include "ipc_server.h"
#include "discord_gateway.h"
#include "title_db.h"
#include "presence_state.h"
#include "log.h"

// Port IPC global exposé au plugin .3gx (nom de port kernel : 11 car. max,
// cf svcCreatePort ; "presence:d" = 10 car.). Même mécanisme que "hb:ldr" /
// "err:f" dans Luma3DS (rosalina/source/errdisp.c, service_manager.c).
#define PRESENCE_IPC_PORT "presence:d"

// --- Surcharges libctru pour un contexte sysmodule -----------------------
// Taille du tas principal (newlib/malloc). Le 1er svcControlMemory (tas
// principal) de 3 MiB avait RÉUSSI sur la console (crash dumps 7 & 8), on
// garde donc 3 MiB : ils portent le buffer soc:U (1 MiB, via memalign), la
// base de titres, les buffers TLS mbedtls, jansson et la pile du thread
// gateway. Le tas linéaire est géré ci-dessous (__system_allocateHeaps).
u32 __ctru_heap_size        = 0x300000; // 3 MiB (tas principal / malloc)
u32 __ctru_linear_heap_size = 0;        // inutilisé : voir __system_allocateHeaps

// Globals libctru manipulés par notre __system_allocateHeaps custom.
extern char *fake_heap_start;
extern char *fake_heap_end;
extern u32 __ctru_heap;
extern u32 __ctru_linear_heap;

// CRASH HARDWARE CONFIRMÉ (crash_dump_00000007 PUIS 00000008, 3DS réelle) :
//   initSystem -> __libctru_init -> __system_allocateHeaps -> svcBreak(PANIC),
//   AVANT main() (sp=0x0FFFFFC0). Analyse du désassemblage + des 2 dumps :
//   le 1er svcControlMemory (tas principal, MEMOP_ALLOC) RÉUSSIT, mais le 2e
//   (tas linéaire, op=0x00010003 = MEMOP_ALLOC_LINEAR) ÉCHOUE puis svcBreak
//   (LR des dumps = __system_allocateHeaps+0x1a8, la branche d'échec du 2e
//   svcControlMemory). La vérification « total <= mémoire dispo » passait
//   AVANT (donc ce n'est PAS un problème de budget/taille : mettre le tas
//   linéaire à 256 KiB au lieu de 0 n'a rien changé, cf. dump #8) : c'est
//   l'allocation de mémoire LINÉAIRE elle-même qui est refusée à ce process
//   System/sysapplet par le noyau.
//
// CORRECTIF : on surcharge le symbole faible __system_allocateHeaps de libctru
// (system/allocateHeaps.c) pour n'allouer QUE le tas principal (MEMOP_ALLOC)
// et NE JAMAIS appeler MEMOP_ALLOC_LINEAR. Ce sysmodule n'utilise pas
// linearAlloc/GPU (le buffer soc:U passe par memalign sur le tas principal),
// donc l'absence de tas linéaire est sans conséquence. mappableInit() est
// conservé (région VA 0x10000000-0x14000000) comme dans le code d'origine.
// Source : libctru system/allocateHeaps.c (symbole WEAK, surchargeable) +
// 3ds/allocator/mappable.h ; reproduit d'après le désassemblage de la version
// installée (devkitARM 16 / libctru des portlibs).
void __system_allocateHeaps(void) {
    Result rc = svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0,
                                 __ctru_heap_size, MEMOP_ALLOC, MEMPERM_READWRITE);
    if (R_FAILED(rc)) svcBreak(USERBREAK_PANIC);

    // Tas newlib (malloc/memalign) = tas principal ci-dessus.
    fake_heap_start = (char *)__ctru_heap;
    fake_heap_end   = fake_heap_start + __ctru_heap_size;

    // Pas de tas linéaire (MEMOP_ALLOC_LINEAR refusé pour ce process) :
    // linearAlloc() renverra NULL, ce qui est acceptable ici car non utilisé.
    __ctru_linear_heap = 0;
    __ctru_linear_heap_size = 0;

    // Allocateur d'adresses mappables (mapping de blocs mémoire partagés) :
    // mêmes bornes que le libctru d'origine.
    mappableInit(0x10000000, 0x14000000);
}

// libctru appelle par défaut aptInit()/hidInit() dans __appInit : un
// sysmodule n'est pas une applet APT, on n'initialise que srv + fs + le
// montage "sdmc:" (devoptab libctru 2.x : archiveMountSdmc).
void __appInit(void) {
    srvInit();
    fsInit();
    archiveMountSdmc();
}

void __appExit(void) {
    archiveUnmountAll();
    fsExit();
    srvExit();
}

static presence_state_t g_state;

static void presence_state_init(presence_state_t *st) {
    memset(st, 0, sizeof(*st));
    st->kind = PRESENCE_KIND_UNKNOWN;
}

int main(void) {
    // Sysmodules n'ont pas de sortie graphique — pas de gfxInit ici.
    logInit();
    logPrintf("tricord_presenced start");

    titleDbInit();

    // Au boot, NS (qui héberge APT) peut ne pas être encore prêt : on
    // retente quelques secondes avant de continuer en mode dégradé.
    Result rc = -1;
    for (int i = 0; i < 30 && R_FAILED(rc); i++) {
        rc = aptMonitorInit();
        if (R_FAILED(rc)) svcSleepThread(1000 * 1000000ULL);
    }
    logPrintf("aptMonitorInit rc=%08lX", (unsigned long)rc);

    rc = ipcServerInit(PRESENCE_IPC_PORT);
    logPrintf("ipcServerInit rc=%08lX", (unsigned long)rc);

    rc = discordGatewayInit();      // lit le token via TriCord (ou config.txt en repli)
    logPrintf("discordGatewayInit rc=%08lX", (unsigned long)rc);
    bool gatewayUp = R_SUCCEEDED(rc);
    // TODO(emergent) : si gatewayUp est false ici, c'est très probablement
    // parce que TriCord n'a encore aucun compte connecté (accounts absent).
    // Le sysmodule ne doit PAS s'arrêter pour autant : il continue à tourner
    // (monitoring APT, IPC pour le futur plugin) et retente périodiquement
    // discordGatewayInit() ci-dessous, jusqu'à ce que l'utilisateur se
    // connecte dans TriCord.

    presence_state_init(&g_state);

    int tick = 0;
    // Boucle principale : poll léger, pas de busy-wait. Un sysmodule n'a pas
    // d'aptMainLoop ; la fin de vie est gérée par PM (terminaison au reboot).
    while (true) {
        presence_state_t fresh;
        Result r = aptMonitorGetCurrentState(&fresh);
        if (R_SUCCEEDED(r) && !presenceStateEquals(&fresh, &g_state)) {
            g_state = fresh;
            logPrintf("state kind=%d tid=%016llX name=%s", (int)g_state.kind,
                      (unsigned long long)g_state.title_id, g_state.game_name);
            if (gatewayUp) {
                discordGatewayUpdatePresence(&g_state);
            }
            ipcServerBroadcastState(&g_state);
        }

        // Toutes les ~15s : soit on retente la connexion (TriCord vient de se
        // connecter après le démarrage du sysmodule), soit on vérifie qu'un
        // changement de compte / une déconnexion n'a pas eu lieu.
        if (tick % 15 == 0) {
            if (!gatewayUp) {
                rc = discordGatewayInit();
                if (R_SUCCEEDED(rc)) {
                    logPrintf("Gateway démarrée après connexion tardive de TriCord");
                    gatewayUp = true;
                }
            } else if (discordGatewayRefreshTokenIfChanged()) {
                // TODO(emergent) : la reconnexion réelle n'est pas encore
                // implémentée dans discord_gateway.c (voir son TODO) ; pour
                // l'instant on redémarre proprement tout le cycle.
                discordGatewayExit();
                gatewayUp = R_SUCCEEDED(discordGatewayInit());
            }
        }

        ipcServerPoll();
        svcSleepThread(1000 * 1000000ULL); // 1 s : APT n'est interrogé qu'une fois/s
        tick++;
    }

    ipcServerExit();
    discordGatewayExit();
    aptMonitorExit();
    titleDbExit();
    return 0;
}
