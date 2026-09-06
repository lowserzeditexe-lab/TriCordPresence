# Architecture — TriCord Presence

## Objectif

Afficher le jeu 3DS en cours sur le profil Discord (Rich Presence), sans
jamais demander de token à l'utilisateur : le compte Discord réutilisé est
celui déjà connecté dans TriCord.

## Pourquoi ce n'est pas un "plugin TriCord"

TriCord (dépôt officiel) n'expose aucune API de plugin, IPC, socket local
ou mécanisme d'extension — c'est un homebrew monolithique (.cia), un seul
process. Il n'y a donc rien à quoi "se brancher" depuis l'extérieur.

Ce projet est un **sysmodule indépendant** qui tourne à côté de TriCord et
qui lit le compte que TriCord a déjà stocké sur la carte SD
(`sdmc:/3ds/TriCord/accounts`), plutôt que de demander un second token à
l'utilisateur. Voir `ACCOUNT_BRIDGE.md` pour le détail technique et les
limites de cette approche.

## Composants

| Composant     | Rôle                                                              | Process |
|---------------|--------------------------------------------------------------------|---------|
| `sysmodule`   | Lit le compte TriCord, surveille le jeu en cours (APT), maintient la connexion Discord Gateway, met à jour la Rich Presence | tourne en arrière-plan en permanence |
| `plugin`      | (Phase 2) Overlay `.3gx` Luma3DS : affiche un HUD "🎮 jeu lancé" par-dessus le jeu | injecté dans le process du jeu au lancement |
| `installer`   | Homebrew CLI (.3dsx/.cia) : copie `sysmodule` + `plugin` aux bons endroits sur la SD | lancé une fois par l'utilisateur |

## Flux de données

```
sdmc:/3ds/TriCord/accounts (chiffré, écrit par TriCord)
        │  PS_EncryptDecryptAes / PS_KEYSLOT_0D
        ▼
tricord_account_bridge (sysmodule)
        │  token du compte actif
        ▼
discord_gateway (sysmodule)  ◄── apt_monitor (Title ID du jeu en cours)
        │  Update Presence (op 3)
        ▼
Discord Gateway (wss://gateway.discord.gg)
```

Le futur canal pop-up (phase 2) :

```
apt_monitor détecte un changement de jeu
        │
        ▼
ipc_server (sysmodule, port nommé local)
        │
        ▼
presence_client (plugin .3gx, injecté dans le jeu)
        │
        ▼
overlay_draw → HUD "🎮 <jeu> vient d'être lancé"
```

## Ce que ce sysmodule ne fait PAS

- Pas de serveur externe obligatoire — tout est local à la console.
- Pas de bot Discord — c'est le compte utilisateur lui-même qui se
  connecte (comme TriCord), pas une application bot séparée.
- Pas de saisie de token — voir `ACCOUNT_BRIDGE.md`.
- Pas de modification de TriCord — les deux tournent en parallèle,
  indépendamment, sans se toucher.

## Risque à connaître

Maintenir une connexion Gateway avec un token utilisateur (au lieu d'un
token bot) correspond, au sens des CGU Discord, à un usage de type
"self-bot". C'est déjà le cas de TriCord lui-même ; ce projet ouvre une
**deuxième** session du même type sur le même compte. Discord supporte
nativement le multi-session (comme téléphone + PC), donc ça fonctionne
techniquement, mais le risque CGU s'applique aux deux connexions.
