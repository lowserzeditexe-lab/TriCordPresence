# TriCord Presence

Rich Presence Discord pour 3DS ("🎮 en train de jouer à ...") sans jamais
demander de token : le compte réutilisé est celui déjà connecté dans
TriCord. Voir `docs/ARCHITECTURE.md` et `docs/ACCOUNT_BRIDGE.md`.

## Livrable

`build.sh` produit `dist/tricord-presence-installer.cia` — un installeur
CLI (pas d'interface graphique pour cette version) qui copie tout le
nécessaire sur la carte SD.

```
├── sysmodule/   → tourne en arrière-plan : lit le compte TriCord, surveille
│                  le jeu en cours (APT), maintient la connexion Discord
│                  Gateway, met à jour la Rich Presence
├── plugin/      → (phase 2) overlay .3gx Luma3DS pour les pop-up "jeu lancé"
├── installer/   → homebrew CLI (.3dsx + .cia) : copie sysmodule + plugin +
│                  base de titres sur la SD, sans écran de saisie de token
├── tools/       → scripts de build (bundle CA, base de titres, assets, QR)
└── docs/        → architecture, mécanisme du pont de compte
```

## Compiler

```
source tools/env.sh   # ou exporte DEVKITARM toi-même
./build.sh
```

Prérequis : devkitARM + devkitPro (`3ds-dev`, `3ds-mbedtls`, `3ds-wslay`,
`3ds-jansson`) + `makerom`/`3gxtool`/`bannertool` (voir
`tools/build_host_tools.sh`).

## Installer sur la console

**Prérequis : TriCord déjà installé et connecté à un compte Discord.**

1. Copier `dist/tricord-presence-installer.cia` sur la SD et l'installer
   via FBI (ou tout gestionnaire de CIA).
2. Lancer l'installeur : il copie le sysmodule vers `/luma/sysmodules/`,
   le plugin vers `/luma/plugins/`, et propose de lancer le sysmodule
   sans reboot.
3. Dans Luma3DS : activer "Enable loading external FIRMs and modules"
   (menu config Luma, SELECT au boot) et, dans Rosalina (L+Bas+Select),
   le "Plugin loader".
4. Relancer l'installeur après chaque redémarrage (Luma3DS ne relance pas
   un sysmodule custom tout seul au boot — voir `docs/EMERGENT_PROMPT.md`
   pour les pistes d'amélioration).

Si TriCord n'a pas encore de compte connecté au moment de l'installation,
le sysmodule démarre quand même et attend — rien à refaire manuellement,
il se connectera automatiquement dès que TriCord aura un compte actif.

## État

Rien de tout ceci n'a été testé sur console ni sur émulateur (pas de
hardware/Citra-Azahar disponibles pendant l'écriture de ce squelette). Les
zones marquées `TODO(hw)` dans le code sont implémentées et sourcées
(3dbrew.org, code source TriCord) mais non confirmées en exécution réelle.
Testez d'abord avec un **compte Discord jetable**, pas votre compte
principal (risque CGU documenté dans `docs/ARCHITECTURE.md`).
