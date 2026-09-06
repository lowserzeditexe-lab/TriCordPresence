# Prompt pour Emergent — build d'un .cia installable (TriCord Presence)

Copie-colle le bloc ci-dessous dans Emergent.

---

Je te fournis un squelette de projet homebrew Nintendo 3DS (CFW Luma3DS)
appelé "TriCord Presence" : Rich Presence Discord pour 3DS, sans jamais
demander de token à l'utilisateur (le compte réutilisé est celui déjà
connecté dans TriCord — voir `docs/ARCHITECTURE.md` et
`docs/ACCOUNT_BRIDGE.md` avant de commencer).

## Objectif de cette itération

**Livrer un `.cia` compilé et fonctionnel** : `./build.sh` doit produire
`dist/tricord-presence-installer.cia`, un installeur en ligne de commande
(pas d'interface graphique) qui, une fois installé et lancé sur la
console, copie le sysmodule et le plugin aux bons endroits sur la carte SD
et peut lancer le sysmodule sans reboot. C'est le seul livrable qui compte
pour cette itération — le reste (pop-up en jeu, etc.) est phase 2.

## Environnement

1. Installer devkitARM + devkitPro (`dkp-pacman -S 3ds-dev 3ds-mbedtls
   3ds-wslay 3ds-jansson`). Si `apt.devkitpro.org` répond en 403 depuis cet
   environnement, essaie de récupérer l'image Docker officielle
   `devkitpro/devkitarm` et d'en extraire les layers — sinon documente le
   blocage précisément (message d'erreur exact, domaine bloqué) et passe
   au maximum de ce qui est compilable sans ça.
2. `tools/build_host_tools.sh` pour makerom / 3gxtool / bannertool /
   smdhtool / 3dsxtool.
3. `./build.sh` clone et compile CTRPluginFramework automatiquement au
   premier lancement (nécessaire au plugin `.3gx`, phase 2 — s'il bloque,
   voir point 4 ci-dessous).

## Si la compilation complète échoue

Ordre de préférence :
1. **Compile le maximum indépendamment** : le sysmodule et l'installeur ne
   dépendent que de libctru/mbedtls/wslay/jansson, pas de CTRPluginFramework.
   Si seul le plugin bloque, commente son build dans `build.sh`
   (`make -C "$ROOT/plugin"`) et assemble quand même l'installeur avec
   uniquement le sysmodule dedans (le `.cia` reste utile sans overlay —
   documente ce choix dans le rapport).
2. Si même le sysmodule ne compile pas, livre le code + un rapport précis
   (erreurs de compilation, dépendances manquantes) plutôt que de deviner
   des correctifs non testés.

## Contraintes impératives

1. **Ne jamais faire saisir de token à l'utilisateur en usage normal.**
   Le token vient de `sysmodule/source/tricord_account_bridge.c` (compte
   déjà connecté dans TriCord). `installer/source/main.c` ne propose une
   saisie manuelle que si `sdmc:/3ds/TriCord/accounts` est absent, en mode
   dépannage explicite — ne change pas ce comportement.
2. Ne supprime aucun `TODO(hw)` sans l'avoir réellement testé sur
   hardware/Citra-Azahar. Si tu ne peux pas tester, implémente quand même
   le code (documenté, sourcé 3dbrew/code TriCord) et laisse le TODO en
   place plutôt que de deviner silencieusement.
3. Documente, pour chaque point incertain que tu résous, la source
   utilisée (3dbrew.org, code d'un projet homebrew existant, etc.).
4. Ne code jamais de token/secret en dur, et ne le fais jamais transiter
   par un service externe autre que la Gateway Discord elle-même.
5. Le risque CGU Discord (connexion permanente avec un token utilisateur)
   est un choix déjà assumé — pas besoin de proposer une alternative bot.
6. Ne restructure pas l'arborescence des dossiers sans raison technique
   forte — si tu dois le faire, explique pourquoi dans le rapport.

## Points spécifiques à vérifier/corriger en priorité

1. `sysmodule/tricord_presenced.rsf` : le `Dependency: ps:` est un
   placeholder (`0x0000000000000000`) — trouve/vérifie le vrai Title ID du
   module PS avant de builder, sinon `GetServiceHandle("ps:ps")` peut
   bloquer indéfiniment au démarrage du sysmodule.
2. `tools/gen_qr.py` est volontairement non-bloquant (`|| true` dans
   `build.sh`) — ne le rends pas bloquant, ce n'est pas essentiel au
   livrable.
3. `tricord_account_bridge.c` : si tu as accès à une vraie 3DS avec
   TriCord installé et connecté, teste `accountBridgeGetToken` contre le
   vrai fichier `accounts` généré — c'est le point le plus critique du
   projet (voir `docs/ACCOUNT_BRIDGE.md`).
4. `discord_gateway.c`, fonction `discordGatewayRefreshTokenIfChanged` :
   stub qui détecte un changement de compte mais ne fait qu'un cycle
   exit/init brutal (voir `main.c`) — améliore si le temps le permet,
   sinon laisse tel quel et note-le dans le rapport, ce n'est pas
   bloquant pour le livrable de cette itération.

## Rapport attendu

- `docs/RAPPORT_IMPLEMENTATION.md` dans le projet, ET un résumé dans ta
  réponse : ce qui compile, ce qui a été testé où, ce qui reste en
  `TODO(hw)` faute de hardware, tout point deviné faute de documentation,
  et l'emplacement exact du `.cia` produit (ou, à défaut, la raison précise
  pour laquelle il n'a pas pu être produit).
