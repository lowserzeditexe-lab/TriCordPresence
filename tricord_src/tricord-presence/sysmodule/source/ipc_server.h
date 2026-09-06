#pragma once
#include <3ds/types.h>
#include "presence_state.h"

// Petit serveur IPC local, exposé sous un nom de port global ("presence:d"),
// qui permet au plugin .3gx (chargé dans le process du jeu) de demander
// "quel est l'état de présence actuel ?" pour afficher son overlay.
//
// Choix : port global kernel (svcCreatePort nommé) et NON service srv:,
// car le plugin tourne dans le process du jeu dont l'exheader ne connaît
// évidemment pas notre service ; svcConnectToPort n'est soumis à aucune
// ACL de service (même mécanisme que "hb:ldr" de Luma3DS/Rosalina).
// Le sysmodule a besoin des SVC CreatePort/AcceptSession/ReplyAndReceive
// (déclarés dans tricord_presenced.rsf).

Result ipcServerInit(const char *portName);
void ipcServerPoll(void);
void ipcServerBroadcastState(const presence_state_t *state);
void ipcServerExit(void);
