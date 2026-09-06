#pragma once
#include <stdbool.h>
#include "presence_state.h"

// Client IPC côté plugin : interroge le sysmodule tricord_presenced via le
// port global "presence:d" (svcConnectToPort + svcSendSyncRequest, aucun
// droit de service exheader requis — voir presence_client.c).
// TODO(hardware): valider sur console que Luma3DS n'interdit pas
// svcConnectToPort vers un port non-système depuis un process jeu (a priori
// non : c'est le mécanisme de "hb:ldr"/"plg:ldr").

#ifdef __cplusplus
extern "C" {
#endif

void presenceClientInit(void);
bool presenceClientPoll(presence_state_t *out); // true si un état valide a été lu
void presenceClientExit(void);

#ifdef __cplusplus
}
#endif
