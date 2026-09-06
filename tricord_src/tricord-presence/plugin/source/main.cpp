/*
 * tricord_overlay — plugin Luma3DS (.3gx)
 *
 * Chargé dans le process du jeu en cours (slot "default", donc dans TOUS
 * les jeux si l'utilisateur a activé le plugin loader). Rôle unique :
 * afficher un petit HUD "Vous jouez à <jeu>" quand l'état remonté par le
 * sysmodule tricord_presenced change.
 *
 * Framework : CTRPluginFramework (CTRPF, gitlab.com/thepixellizeross/
 * ctrpluginframework — la lib de Nanquitas utilisée par la quasi-totalité
 * des plugins .3gx). Elle fournit :
 *   - le cycle de vie d'un plugin (pas de gfxInit propriétaire, cohabite
 *     avec le rendu du jeu hôte : pluginInit.cpp),
 *   - l'overlay : OSD::Run(callback) appelé à chaque swap de framebuffer via
 *     un hook MITM sur la routine GSP du jeu (OSDImpl.cpp), cf overlay_draw.cpp.
 * C'est exactement ce que le squelette demandait de réutiliser plutôt que de
 * réécrire un hook GSP depuis zéro.
 */

#include <3ds.h>
#include <string.h>
#include <CTRPluginFramework.hpp>
#include "presence_state.h" // copie du header partagé avec le sysmodule
#include "overlay_draw.h"
#include "presence_client.h"

namespace CTRPluginFramework {

// Appelé par CTRPF avant main(), pendant le chargement du jeu.
void PatchProcess(FwkSettings &settings) {
    settings.WaitTimeToBoot = Seconds(5); // laisser le jeu initialiser GSP
    settings.AllowActionReplay = false;   // pas de menu de cheats
    settings.AllowSearchEngine = false;
}

static presence_state_t s_lastShown;
static Clock s_pollClock;

// Callback exécuté à chaque frame par PluginMenu::Run (menu fermé ou non).
static void pollPresence(void) {
    if (!s_pollClock.HasTimePassed(Seconds(1))) return; // 1 requête IPC / s
    s_pollClock.Restart();

    presence_state_t current;
    if (!presenceClientPoll(&current)) return;
    if (presenceStateEquals(&current, &s_lastShown)) return;

    s_lastShown = current;
    if (current.kind == PRESENCE_KIND_IN_GAME)
        overlayShowToast("Vous jouez a", current.game_name);
}

int main(void) {
    PluginMenu *menu = new PluginMenu("TriCord Presence", 0, 1, 0,
        "Affiche 'Vous jouez à <jeu>' quand le sysmodule tricord_presenced\n"
        "met à jour votre présence Discord.");
    menu->SynchronizeWithFrame(true);
    menu->ShowWelcomeMessage(false);

    memset(&s_lastShown, 0, sizeof(s_lastShown));
    presenceClientInit();   // ouvre le port global "presence:d"
    overlayInit();          // enregistre le callback OSD (hook GSP de CTRPF)

    menu->Callback(pollPresence);
    int ret = menu->Run();  // boucle jusqu'à la fin du process jeu

    overlayExit();
    presenceClientExit();
    delete menu;
    return ret;
}

} // namespace CTRPluginFramework
