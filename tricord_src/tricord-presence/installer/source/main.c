/*
 * tricord-presence-installer — homebrew .3dsx / .cia, mode CLI uniquement
 *
 * Volontairement sans interface graphique pour cette version : juste une
 * console texte (consoleInit) qui log chaque étape et attend une validation
 * clavier avant les actions destructives (copie/écrasement de fichiers).
 *
 * Les fichiers à installer (sysmodule .cxi, plugin .3gx, base titles.txt)
 * sont embarqués dans le romfs de l'installeur (installer/romfs/, rempli par
 * build.sh). Pas d'assets visuels dans ce romfs.
 */

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

// Title ID du sysmodule (voir sysmodule/tricord_presenced.rsf). Luma3DS
// exige que le fichier s'appelle <TitleID>.cxi dans /luma/sysmodules/
// (Luma3DS sysmodules/loader/source/patcher.c, openSysmoduleCxi).
#define SYSMODULE_TID_HI 0x00040130u
#define SYSMODULE_TID_LO 0x0F000102u
#define SYSMODULE_DEST "sdmc:/luma/sysmodules/000401300F000102.cxi"
// default.3gx = plugin chargé dans tous les jeux (Luma3DS plugin loader).
// Écrase un éventuel plugin "default" déjà installé -> on prévient avant.
#define PLUGIN_DEST    "sdmc:/luma/plugins/default.3gx"
#define CONFIG_DIR     "sdmc:/3ds/tricord-presence"
#define CONFIG_FILE    CONFIG_DIR "/config.txt"
#define TITLES_FILE    CONFIG_DIR "/titles.txt"
#define LOG_FILE       CONFIG_DIR "/log.txt"
// exheader.bin résiduels d'anciens essais d'auto-boot (méthode NON retenue,
// cf RAPPORT.md) : NS (0004013000008002) et notre propre Title ID.
#define EXH_NS_FILE    "sdmc:/luma/titles/0004013000008002/exheader.bin"
#define EXH_SELF_FILE  "sdmc:/luma/titles/000401300F000102/exheader.bin"

static bool fileExists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// mkdir récursif sur sdmc: (newlib + devoptab libctru). EEXIST n'est pas
// une erreur.
static bool ensureDir(const char *path) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + strlen("sdmc:/"); *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
            printf("  ERREUR mkdir %s (errno %d)\n", tmp, errno);
            return false;
        }
        *p = '/';
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
        printf("  ERREUR mkdir %s (errno %d)\n", tmp, errno);
        return false;
    }
    return true;
}

static bool copyFile(const char *src, const char *dst) {
    printf("  copie: %s\n         -> %s\n", src, dst);
    FILE *in = fopen(src, "rb");
    if (!in) { printf("  ERREUR: source introuvable (romfs)\n"); return false; }
    FILE *out = fopen(dst, "wb");
    if (!out) { printf("  ERREUR: impossible d'ecrire (errno %d)\n", errno); fclose(in); return false; }

    static char buf[64 * 1024];
    size_t total = 0, n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
        total += n;
    }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    printf(ok ? "  OK (%u octets)\n" : "  ERREUR d'ecriture\n", (unsigned)total);
    return ok;
}

static bool promptYesNo(const char *question) {
    printf("%s [A = oui / B = non]\n", question);
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_A) return true;
        if (kDown & KEY_B) return false;
        gspWaitForVBlank();
    }
    return false;
}

// Saisie du token Discord via l'applet clavier système (libctru swkbd).
// Le token est écrit dans config.txt ("token=..."), jamais dans le binaire.
static bool promptToken(char *out, size_t outSize) {
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, (int)outSize - 1);
    swkbdSetHintText(&swkbd, "Token Discord (compte utilisateur)");
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetFeatures(&swkbd, SWKBD_FIXED_WIDTH);
    swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE_DELAY);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Annuler", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "Valider", true);

    SwkbdButton btn = swkbdInputText(&swkbd, out, outSize);
    if (btn != SWKBD_BUTTON_CONFIRM) return false;
    // Nettoyage : espaces/retours parasites
    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\n' || out[len - 1] == '\r')) out[--len] = '\0';
    return len > 0;
}

static bool writeConfig(const char *token) {
    FILE *f = fopen(CONFIG_FILE, "w");
    if (!f) { printf("  ERREUR: ecriture %s\n", CONFIG_FILE); return false; }
    fprintf(f, "# TriCord Presence - config\n");
    fprintf(f, "# Ce fichier contient votre token Discord : ne le partagez jamais.\n");
    fprintf(f, "token=%s\n", token);
    fprintf(f, "# Jeux a ne jamais publier (Title ID hex, une ligne par jeu ou separes par des virgules) :\n");
    fprintf(f, "# exclude=0004000000030700\n");
    fclose(f);
    return true;
}

// Lancement immédiat du sysmodule sans reboot, méthode "Plug-n-play"
// (zaksabeast/3ds-Plug-n-play, launcher/source/main.cpp) : on "vole" une
// session pm:app via l'extension kernel Luma3DS svcControlService
// (SERVICEOP_STEAL_CLIENT_SESSION, SVC 0xB0, cf Luma3DS rosalina/include/
// csvc.h) puis pm:app LaunchTitle (cmd 0x0001, libctru services/pmapp.c).
// PM demande alors le titre au loader, qui trouve /luma/sysmodules/<TID>.cxi.
// TODO(hardware): non validé sur console. Nécessite Luma3DS >= 12 avec
// "Enable loading external FIRMs and modules" activé.
extern Result svcControlService(u32 op, ...); // csvc.s
static Result launchSysmoduleNow(void) {
    Handle pm = 0;
    Result rc = svcControlService(0 /* SERVICEOP_STEAL_CLIENT_SESSION */, &pm, "pm:app");
    if (R_FAILED(rc)) return rc;

    FS_ProgramInfo info;
    memset(&info, 0, sizeof(info));
    info.programId = ((u64)SYSMODULE_TID_HI << 32) | SYSMODULE_TID_LO;
    info.mediaType = MEDIATYPE_NAND;

    u32 *cmdbuf = getThreadCommandBuffer();
    cmdbuf[0] = IPC_MakeHeader(0x0001, 5, 0); // PMAPP_LaunchTitle
    memcpy(&cmdbuf[1], &info, sizeof(info));
    cmdbuf[5] = PMLAUNCHFLAG_LOAD_DEPENDENCIES;
    rc = svcSendSyncRequest(pm);
    if (R_SUCCEEDED(rc)) rc = (Result)cmdbuf[1];
    svcCloseHandle(pm);
    return rc;
}

static bool removeIfExists(const char *path) {
    if (!fileExists(path)) { printf("  absent : %s\n", path); return true; }
    if (remove(path) != 0) { printf("  ERREUR suppression %s (errno %d)\n", path, errno); return false; }
    printf("  supprime: %s\n", path);
    return true;
}

// Désinstallation : retire tout ce que l'installeur a pu écrire, avec
// confirmation par groupe. Le sysmodule déjà lancé en mémoire s'arrête au
// prochain reboot (pas de terminaison à chaud : il faudrait pm:app
// TerminateTitle, non implémenté).
static void uninstall(void) {
    bool ok = true;
    printf("\n=== Desinstallation ===\n");

    if (promptYesNo("\n[1/4] Supprimer le sysmodule (/luma/sysmodules/000401300F000102.cxi) ?"))
        ok &= removeIfExists(SYSMODULE_DEST);

    if (fileExists(PLUGIN_DEST)) {
        printf("\n[2/4] %s\n  (impossible de verifier qu'il s'agit bien du plugin TriCord)\n", PLUGIN_DEST);
        if (promptYesNo("  Supprimer ce plugin 'default' ?")) ok &= removeIfExists(PLUGIN_DEST);
    } else {
        printf("\n[2/4] plugin absent.\n");
    }

    if (promptYesNo("\n[3/4] Supprimer la config (token !), la base de titres et le log ?")) {
        ok &= removeIfExists(CONFIG_FILE);
        ok &= removeIfExists(TITLES_FILE);
        ok &= removeIfExists(LOG_FILE);
        if (rmdir(CONFIG_DIR) == 0) printf("  supprime: %s\n", CONFIG_DIR);
    }

    if (fileExists(EXH_NS_FILE) || fileExists(EXH_SELF_FILE)) {
        printf("\n[4/4] exheader.bin residuel(s) detecte(s) dans /luma/titles/ :\n");
        if (fileExists(EXH_NS_FILE))   printf("  %s\n", EXH_NS_FILE);
        if (fileExists(EXH_SELF_FILE)) printf("  %s\n", EXH_SELF_FILE);
        if (promptYesNo("  Les supprimer (recommande) ?")) {
            ok &= removeIfExists(EXH_NS_FILE);
            ok &= removeIfExists(EXH_SELF_FILE);
            rmdir("sdmc:/luma/titles/0004013000008002");
            rmdir("sdmc:/luma/titles/000401300F000102");
        }
    } else {
        printf("\n[4/4] aucun exheader.bin residuel.\n");
    }

    printf(ok ? "\nDesinstallation terminee. Redemarrez la console.\n"
              : "\nDesinstallation terminee AVEC ERREURS (voir ci-dessus).\n");
}

static int promptMode(void) {
    printf("[A] Installer   [X] Desinstaller   [B] Quitter\n");
    while (aptMainLoop()) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_A) return 1;
        if (k & KEY_X) return 2;
        if (k & KEY_B) return 0;
        gspWaitForVBlank();
    }
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    Result romfsRc = romfsInit();

    printf("=== TriCord Presence - Installeur (CLI) ===\n\n");
    printf("Cet outil va :\n");
    printf("  1) copier le sysmodule vers /luma/sysmodules/\n");
    printf("  2) copier le plugin overlay vers /luma/plugins/\n");
    printf("  3) copier la base de titres\n\n");
    printf("Aucun token Discord n'est demande : le sysmodule recupere le\n");
    printf("compte deja connecte dans TriCord (voir docs/ACCOUNT_BRIDGE.md).\n");
    printf("Assurez-vous que TriCord est deja installe et connecte AVANT\n");
    printf("de lancer le sysmodule pour la premiere fois.\n\n");
    int mode = promptMode();
    if (mode == 0) { printf("\nAnnule par l'utilisateur.\n"); goto wait_exit; }
    if (mode == 2) { uninstall(); goto wait_exit; }

    if (R_FAILED(romfsRc)) {
        printf("ERREUR: romfs indisponible (%08lX) - build incomplet ?\n", (unsigned long)romfsRc);
        goto wait_exit;
    }

    bool ok = true;

    printf("\n[1/3] Sysmodule...\n");
    ok &= ensureDir("sdmc:/luma/sysmodules");
    ok &= copyFile("romfs:/000401300F000102.cxi", SYSMODULE_DEST);

    printf("\n[2/3] Plugin overlay (phase 2 - DESACTIVE)...\n");
    // L'overlay .3gx (pop-up en jeu) est la PHASE 2. Le format .3gx produit
    // n'est pas accepte par le plugin loader de Luma recent : installe en
    // default.3gx, il provoquait l'erreur "Outdated plugin file" (0xD8E07402)
    // A CHAQUE lancement de jeu. La Rich Presence ne depend PAS de ce plugin
    // (tout se passe dans le sysmodule) : on NE l'installe donc PAS ici, et on
    // propose de retirer un ancien default.3gx qui bloquerait les jeux.
    printf("  Overlay non installe (evite l'erreur 0xD8E07402 en jeu).\n");
    if (fileExists(PLUGIN_DEST)) {
        printf("  Un %s existe deja.\n", PLUGIN_DEST);
        if (promptYesNo("  Le supprimer (recommande, stoppe l'erreur en jeu) ?"))
            ok &= removeIfExists(PLUGIN_DEST);
    }

    printf("\n[3/3] Configuration...\n");
    ok &= ensureDir(CONFIG_DIR);
    ok &= copyFile("romfs:/titles.txt", TITLES_FILE);

    // Pas de saisie de token en usage normal : le sysmodule récupère le
    // compte déjà connecté dans TriCord au démarrage (tricord_account_bridge,
    // voir docs/ACCOUNT_BRIDGE.md). On propose juste, si TriCord n'a
    // manifestement pas encore été connecté, un mode dépannage optionnel.
    if (!fileExists("sdmc:/3ds/TriCord/accounts")) {
        printf("\n  ATTENTION: aucun compte TriCord detecte pour l'instant\n");
        printf("  (sdmc:/3ds/TriCord/accounts absent). Le sysmodule va\n");
        printf("  demarrer quand meme et attendra que vous vous connectiez\n");
        printf("  dans TriCord - rien a faire de plus dans ce cas.\n");
        if (promptYesNo("\n  [mode depannage] Saisir un token manuellement quand meme ?")) {
            static char token[256];
            if (promptToken(token, sizeof(token))) {
                ok &= writeConfig(token);
                memset(token, 0, sizeof(token));
                printf("  token enregistre dans %s (repli de secours uniquement,\n"
                       "  ignore des que TriCord aura un compte connecte).\n", CONFIG_FILE);
            } else {
                printf("  saisie annulee.\n");
            }
        }
    } else {
        printf("\n  Compte TriCord detecte : rien a saisir, la presence\n");
        printf("  suivra automatiquement ce compte.\n");
    }

    printf(ok ? "\nInstallation terminee.\n" : "\nInstallation terminee AVEC ERREURS (voir ci-dessus).\n");
    printf("\nA faire :\n");
    printf(" - Luma3DS : activer \"Enable loading external FIRMs and modules\"\n");
    printf("   (menu de config Luma, SELECT au boot).\n");
    printf(" - Rosalina (L+Bas+Select) : activer \"Plugin loader\".\n");

    if (ok && promptYesNo("\nLancer le sysmodule maintenant (sans reboot) ?")) {
        Result rc = launchSysmoduleNow();
        if (R_SUCCEEDED(rc)) printf("  sysmodule lance.\n");
        else printf("  echec du lancement (%08lX) : redemarrez la console\n  et relancez cet installeur.\n", (unsigned long)rc);
    }
    printf("\nNOTE: Luma3DS ne relance pas un sysmodule custom tout seul au\n");
    printf("boot ; relancez cet installeur apres chaque redemarrage\n");
    printf("(voir RAPPORT.md, \"Lancement au boot\").\n");

wait_exit:
    printf("\n(appuyez sur START pour quitter)\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    romfsExit();
    gfxExit();
    return 0;
}
