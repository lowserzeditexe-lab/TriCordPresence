# Pont de compte avec TriCord

## Le mécanisme

1. TriCord stocke ses comptes dans `sdmc:/3ds/TriCord/accounts` :
   `{"currentIndex":N,"accounts":[{"name":"...","token":"..."},...]}`,
   chiffré en AES-CTR (IV nul) avec `PS_KEYSLOT_0D`.
   Source : dépôt officiel TriCord, `source/core/config.cpp`, fonction
   `encrypt_decrypt_data`.
2. `PS_KEYSLOT_0D` est une clé **liée à la console**, pas au compte
   Discord ni à TriCord spécifiquement. Tout homebrew tournant sur la
   même 3DS, avec le service `ps:ps`, peut appeler
   `PS_EncryptDecryptAes` avec ce keyslot — c'est l'appel que TriCord
   utilise lui-même.
3. `tricord_account_bridge` (dans ce sysmodule) fait exactement cet
   appel sur ce même fichier, et en extrait le token du compte à
   `currentIndex`.

**Ce n'est pas une API fournie par TriCord** : c'est une lecture locale
du même fichier, avec le même service système public. Aucune donnée ne
quitte la console à cette étape ; seule la Gateway Discord (étape
suivante) fait du réseau.

## Implications à assumer

- **Aucune saisie de token** tant que TriCord est déjà connecté — objectif
  atteint.
- **Deux sessions Gateway simultanées** sur le même compte (TriCord +
  ce sysmodule). Techniquement supporté par Discord, mais double
  l'exposition au risque CGU déjà noté dans `ARCHITECTURE.md`.
- **Couplage à un format non documenté publiquement.** Si une future
  version de TriCord change le chemin, le format JSON ou l'algorithme,
  ce module cesse de fonctionner et doit être resynchronisé sur le
  nouveau `config.cpp` de TriCord.
- **Pas de verrou de fichier** entre les deux process : une lecture peut
  tomber pendant une écriture de TriCord. D'où un retry avec backoff
  plutôt qu'un échec fatal (voir `tricord_account_bridge.h`).

## Ce qui reste non vérifié sur hardware

- Accès effectif au service `ps:ps` depuis un sysmodule tiers (droits
  exheader corrects, module PS déjà démarré au bon moment).
- Title ID exact du module PS pour l'entrée `Dependency:` du `.rsf`
  (placeholder à corriger — voir le TODO dans
  `sysmodule/tricord_presenced.rsf`).
- Comportement réel de `PS_EncryptDecryptAes` sur un fichier `accounts`
  produit par une vraie build TriCord (le format a été lu dans le code
  source, jamais exécuté ici faute de hardware/émulateur disponibles).

Ces points sont marqués `TODO(hw)` dans le code : l'implémentation est
écrite et sourcée (3dbrew / code TriCord), mais non confirmée en
exécution réelle.
