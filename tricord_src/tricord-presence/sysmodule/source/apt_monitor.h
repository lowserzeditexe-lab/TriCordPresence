#pragma once
#include <3ds/types.h>
#include "presence_state.h"

// Wrapper autour du service APT (hébergé par NS) pour déterminer :
//   - si l'application au premier plan est le menu HOME ou un jeu
//   - le Title ID du jeu actif, le cas échéant
//
// Implémentation : APT:GetAppletManInfo (0x0005) pour l'AppID actif, puis
// APT:GetAppletInfo (0x0006, AppID 0x300 = Application) pour le Title ID.
// Sources : https://www.3dbrew.org/wiki/APT:GetAppletManInfo,
//           https://www.3dbrew.org/wiki/APT:GetAppletInfo,
//           libctru services/apt.h (NS_APPID, APT_GetAppletInfo).
// TODO(hardware): non validé sur console/Citra (cf apt_monitor.c).

// Lit aussi les lignes "exclude=<TitleID>" de config.txt : un jeu exclu est
// remonté comme PRESENCE_KIND_IDLE (jamais publié sur Discord ni affiché).
Result aptMonitorInit(void);
Result aptMonitorGetCurrentState(presence_state_t *out);
Result aptMonitorGetActiveTitleId(u64 *outTitleId);
void aptMonitorExit(void);
