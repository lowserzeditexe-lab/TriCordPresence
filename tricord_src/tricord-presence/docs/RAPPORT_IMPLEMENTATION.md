# Rapport d'implémentation — build du `.cia` TriCord Presence

Itération : produire `dist/tricord-presence-installer.cia`, installeur CLI
compilé et fonctionnel. **Résultat : livrable produit avec succès**, plugin
inclus (le fallback « sysmodule seul » n'a pas été nécessaire).

Date du build : 2026-09-06. Toolchain : devkitARM **16.1.0** (GCC 16), libctru
issue de l'image officielle `devkitpro/devkitarm` (variante arm64).

---

## 0. Correctif post-crash hardware (crash_dump_00000007) — tas linéaire

**Symptôme (console réelle)** : après installation du `.cia` et lancement de
l'installeur, en répondant « oui » à « Lancer le sysmodule maintenant (sans
reboot) ? », la 3DS génère un crash dump Luma3DS.

**Analyse du dump** (format Luma3DS v3.1, ARM11 core 1) :
- Process fautif = `tricord_presenced`, Title ID `000401300F000102` (le
  sysmodule lui-même) → **le lancement à chaud a fonctionné**, c'est le
  sysmodule qui panique **au démarrage**.
- `sp = 0x0FFFFFC0` (~64 octets utilisés) → crash **avant `main()`**, pendant
  l'init libctru.
- Chaîne reconstruite depuis PC/LR/stack (symboles de `tricord_presenced.elf`) :
  `initSystem → __libctru_init → __system_allocateHeaps → svcBreak(PANIC)`.
  LR = `0x149e00` = instruction juste après le **2ᵉ** `bl svcBreak` de
  `__system_allocateHeaps`, c.-à-d. la branche d'échec du **2ᵉ
  `svcControlMemory`** = allocation du **tas linéaire**.

**Cause racine** : `sysmodule/source/main.c` fixait `__ctru_linear_heap_size = 0`.
Le désassemblage de `__system_allocateHeaps` montre que libctru interprète
`0` non pas comme « pas de tas linéaire » mais comme **« auto = alloue TOUTE la
mémoire restante committable du process »** (`cmp r1,#0` puis `subeq r2,r2,r3`).
Pour un process `System`/`sysapplet`, ce montant auto dépasse ce que
`svcControlMemory` peut réellement committer → échec → `svcBreak`.

**Correctif** (`main.c`) : tailles de heap explicites, jamais 0.
Le 1ᵉʳ `svcControlMemory` (tas principal, 3 MiB) ayant **réussi** sur la
console, on sait que ~3 MiB sont committables ; on conserve donc un total de
3 MiB, réparti explicitement :
```c
u32 __ctru_heap_size        = 0x2C0000; // 2.75 MiB (memalign : soc 1 MiB, titles, TLS…)
u32 __ctru_linear_heap_size = 0x40000;  // 256 KiB explicite (rien n'utilise linearAlloc)
```
Vérifié statiquement dans le nouvel ELF (`.data`) : `__ctru_linear_heap_size =
0x40000` (non nul → la branche « auto » fautive n'est plus prise).

**Statut** : le correctif supprime la cause exacte du crash observé, mais reste
`TODO(hw)` tant que l'utilisateur n'a pas reconfirmé le démarrage du sysmodule
sur console (impossible à exécuter dans l'environnement de build).

---


## 1. Environnement — comment devkitPro a été obtenu

### Blocage rencontré (documenté comme demandé)

`apt.devkitpro.org` est **inaccessible depuis cet environnement** : Cloudflare
renvoie un **HTTP 403** (challenge navigateur) sur toutes les URL, y compris
avec un User-Agent de navigateur.

- `curl -I https://apt.devkitpro.org/install-devkitpro-pacman` → `HTTP/2 403`
- `curl https://apt.devkitpro.org/` → page « Attention Required! | Cloudflare »
- `curl -I https://apt.devkitpro.org/pool/dkp/linux/aarch64/` → `HTTP/2 403`

`dkp-pacman`/`pacman` ne sont pas installés et n'ont donc **pas pu** être
utilisés (`dkp-pacman -S 3ds-dev ...` impossible). Docker n'est pas disponible
non plus (`docker` absent, pas de daemon).

### Contournement retenu (piste « image Docker » du prompt)

Le prompt suggérait, en cas de 403, de « récupérer l'image Docker officielle
`devkitpro/devkitarm` et d'en extraire les layers ». C'est exactement ce qui a
été fait, **sans daemon Docker**, via l'API HTTP du registry Docker Hub :

1. Jeton anonyme : `auth.docker.io/token?...scope=repository:devkitpro/devkitarm:pull`.
2. Le manifeste `latest` est un index multi-arch. L'hôte étant **aarch64**,
   la variante **arm64** a été choisie (digest
   `sha256:d532dbc32d4ed7412ecf2df2a9710070d0f11a01cc7334e28d102871746533cf`).
   *Important* : les binaires devkitARM tournent sur l'hôte ; il fallait donc
   des binaires **arm64 natifs**, pas la variante amd64 (qui aurait exigé qemu).
3. Les 4 layers (`registry-1.docker.io/v2/devkitpro/devkitarm/blobs/<digest>`,
   ~520 Mo au total) ont été téléchargés puis extraits dans `/opt/dkp_root`,
   ce qui donne `/opt/dkp_root/opt/devkitpro/...`.
4. `tools/env.sh` prévoyait déjà ce chemin de repli
   (`/opt/dkp_root/opt/devkitpro/devkitARM`) — **aucune modification** de la
   structure de dossiers n'a été nécessaire.

Contenu obtenu et vérifié : `devkitARM/` (gcc 16.1.0), `libctru`, `portlibs/3ds`
(avec `libmbedtls`, `libmbedx509`, `libmbedcrypto`, `libwslay`, `libjansson`),
`tools/bin` (3dsxtool, smdhtool, etc.).

> Le script `pull_dkp.sh` utilisé pour ce contournement est laissé à la racine
> `/app` de l'environnement de build (hors dépôt) à titre de trace ; il n'est
> pas nécessaire au dépôt lui-même.

### Outils hôte

`tools/build_host_tools.sh` a fonctionné tel quel (GitHub accessible) et a
compilé, dans `tools/bin/` :

| Outil       | Source                         | Rôle                              |
|-------------|--------------------------------|-----------------------------------|
| `makerom`   | 3DSGuy/Project_CTR             | ELF→CXI (sysmodule) et →CIA       |
| `ctrtool`   | 3DSGuy/Project_CTR             | vérification/inspection du .cia   |
| `3gxtool`   | Nanquitas/3gxtool              | ELF→.3gx (plugin)                 |
| `bannertool`| Epicpkmn11/bannertool          | SMDH + bannière du .cia           |

Dépendance hôte installée : `libyaml-cpp-dev`, `build-essential`, `cmake`
(via `apt-get`, pour `3gxtool`/`bannertool`/Project_CTR). Les patchs aarch64
déjà présents dans `build_host_tools.sh` (bannertool) ont suffi.

---

## 2. Ce qui compile

**Tout compile**, dans l'ordre de `build.sh` :

| Composant                       | Résultat                                  | Taille   |
|---------------------------------|-------------------------------------------|----------|
| CTRPluginFramework (`libctrpf`) | compilée et installée dans `libctrpf/`    | 1.7 Mo   |
| Sysmodule (`.cxi`)              | `000401300F000102.cxi`                    | 482 Ko   |
| Plugin overlay (`.3gx`)         | `tricord_overlay.3gx`                     | 808 Ko   |
| Installeur (`.3dsx`)            | `tricord-presence-installer.3dsx`         | 1.68 Mo  |
| **Installeur (`.cia`)**         | **`tricord-presence-installer.cia`**      | **1.69 Mo** |

Aucune erreur de compilation ; uniquement des warnings `-Wunused-but-set-variable`
internes à CTRPluginFramework (sans incidence). Le code du projet
(sysmodule + plugin + installeur) compile **sans warning** contre libctru 16 /
mbedtls / wslay / jansson des portlibs — les API utilisées
(`PS_EncryptDecryptAes`, `PS_KEYSLOT_0D`, `NDMU_EnterExclusiveState`,
`mbedtls_ssl_conf_min_version`, wslay no-buffering, etc.) sont toutes présentes
dans cette version des portlibs.

### Emplacement exact du livrable

```
tricord-presence/dist/tricord-presence-installer.cia   (1 688 512 octets)
```

(Le même `dist/` contient aussi le `.3dsx`, le `.cxi`, le `.3gx`, `titles.txt`
et `qr.html`, pour copie SD manuelle éventuelle.)

---

## 3. Ce qui a été testé, et où

Testé **sur l'hôte de build (aarch64 Linux)** uniquement — validation
structurelle, pas d'exécution 3DS :

- **`build.sh` de bout en bout** : succès complet, `dist/` produit.
- **`ctrtool` sur le `.cia`** :
  - Title ID installeur = `000400000f000200`, Product code `CTR-P-TRCP`,
    FormType Executable / ContentType Application, ExeFS + RomFS présents.
  - **RomFS embarqué vérifié** : contient exactement les 3 fichiers attendus
    que l'installeur copie sur la SD :
    `000401300F000102.cxi`, `tricord_overlay.3gx`, `titles.txt`.
- **`ctrtool` sur le `.cxi` du sysmodule** :
  - Title ID = `000401300f000102` (= nom de fichier exigé par Luma3DS ✔).
  - `ServiceAccessControl` contient bien `ps:ps`.
  - `Dependency` contient bien `0004013000003102` (le module PS — voir §4),
    aux côtés de `..2402` (ac), `..1702` (cfg), `..2b02` (ndm), `..2e02` (socket).

**Non testé** (aucun hardware ni Citra-Azahar disponible dans l'environnement,
comme annoncé dans le README) : tout comportement à l'exécution sur console.
Ces points restent en `TODO(hw)` (voir §5).

---

## 4. Points incertains résolus, avec source

### 4.1 Title ID du module PS (`tricord_presenced.rsf`, ligne `Dependency: ps:`)

Le placeholder `ps: 0x0000000000000000` a été remplacé par :

```
ps: 0x0004013000003102
```

**Sources :**
- 3dbrew.org, « Title list » / page « PS_Services » : le module système **PS**
  (NATIVE_FIRM) porte le Title ID `0004013000003102`, identique sur toutes les
  régions.
- **Recoupement interne** : cette valeur était **déjà** présente et utilisée
  dans `installer/tricord-presence-installer.rsf`
  (`ps: 0x0004013000003102`), RSF dérivé d'un template d'application officielle.
  Elle est cohérente avec le schéma des autres modules NATIVE_FIRM déjà listés
  dans le même fichier (cfg `..1702`, ndm `..2b02`, socket `..2e02`, ac `..2402`).

C'était le seul placeholder explicitement « ne pas compiler tel quel ». Il est
désormais correct et le `.cxi` a été régénéré avec.

### 4.2 Choix de la variante arm64 de l'image Docker

Deviné/résolu par nécessité technique (hôte aarch64) — voir §1. Documenté ici
car ce n'était pas explicité dans le prompt (qui mentionnait juste
`devkitpro/devkitarm`).

Aucun autre point n'a été « deviné en silence ». Les zones sans certitude
matérielle restent en `TODO(hw)` sans modification hasardeuse.

---

## 5. `TODO(hw)` conservés (implémentés mais non validés sur console)

Conformément à la consigne (« ne supprime aucun `TODO(hw)` sans l'avoir testé
sur hardware/Citra-Azahar »), tous les TODO suivants sont **laissés en place** ;
le code correspondant est écrit et sourcé mais n'a pas pu être exécuté :

1. **`tricord_account_bridge.c` / `PS_EncryptDecryptAes(PS_KEYSLOT_0D)`** — le
   déchiffrement AES-CTR du fichier `sdmc:/3ds/TriCord/accounts` n'a pas pu être
   testé contre un vrai fichier produit par TriCord (pas de 3DS ni de fichier
   `accounts` réel). C'est le point **le plus critique** du projet
   (cf `docs/ACCOUNT_BRIDGE.md`). Le format JSON et l'algo sont repris du code
   source TriCord (`source/core/config.cpp`, `encrypt_decrypt_data`).
2. **Accès effectif à `ps:ps` depuis un sysmodule tiers** (droits exheader
   corrects, module PS démarré au bon moment) — non vérifiable sans console.
   La dépendance PS (§4.1) réduit le risque de blocage sur
   `GetServiceHandle("ps:ps")` mais ne le garantit pas.
3. **`installer/source/main.c` → `launchSysmoduleNow()`** — lancement à chaud du
   sysmodule via `svcControlService(SERVICEOP_STEAL_CLIENT_SESSION, "pm:app")`
   puis `PMAPP_LaunchTitle` (méthode Plug-n-play, zaksabeast/3ds-Plug-n-play).
   Nécessite Luma3DS ≥ 12 avec « Enable loading external FIRMs and modules ».
4. **`discord_gateway.c` / `tls_connect`** — dépend de l'horloge RTC de la
   console pour la validité des certificats (bundle CA embarqué).
5. **`discord_gateway.c` / `ws_handshake`** — vérification de
   `Sec-WebSocket-Accept` omise (optionnelle côté client).
6. **Relance au boot** — Luma3DS ne relance pas un sysmodule custom seul ;
   l'installeur doit être relancé après chaque redémarrage (documenté README).
7. **Allocation heap sysmodule** (`__ctru_heap_size = 3 MiB`, `MemoryType: System`
   + `sysapplet`) — à revalider sur o3DS.

---

## 6. Point laissé en l'état (non bloquant, comme autorisé)

**`discord_gateway.c` / `discordGatewayRefreshTokenIfChanged()`** : reste un
stub qui **détecte** un changement de compte mais ne fait pas de reconnexion
propre (le cycle brutal exit/init est dans `main.c`). Le prompt autorisait
explicitement à le laisser tel quel pour cette itération (« améliore si le
temps le permet, sinon laisse tel quel »). Non modifié pour ne pas introduire
de régression non testable. Le `TODO(emergent)` correspondant reste dans le code.

**`tools/gen_qr.py`** : laissé non-bloquant (`|| true` dans `build.sh`), non
modifié. À ce build il a généré une page texte simple (`dist/qr.html`) faute
d'`QR_URL`.

---

## 7. Contraintes impératives — vérification

1. **Aucune saisie de token en usage normal** : `installer/source/main.c`
   inchangé — la saisie manuelle (`promptToken`) n'est proposée que si
   `sdmc:/3ds/TriCord/accounts` est absent (mode dépannage explicite). ✔
2. **Aucun `TODO(hw)` supprimé** sans test hardware. ✔ (voir §5)
3. **Sources documentées** pour chaque point résolu. ✔ (voir §4)
4. **Aucun token/secret en dur** ; le seul réseau est la Gateway Discord. ✔
5. Risque CGU self-bot assumé, pas d'alternative bot proposée. ✔
6. **Arborescence des dossiers non restructurée.** ✔ (seul le contenu de la
   ligne `Dependency: ps:` du `.rsf` sysmodule a changé, §4.1).

---

## 8. Reproduire le build

```bash
# 1. Toolchain (si apt.devkitpro.org accessible) :
#    dkp-pacman -S 3ds-dev 3ds-mbedtls 3ds-wslay 3ds-jansson
#    Sinon (403 Cloudflare) : extraire l'image arm64 devkitpro/devkitarm
#    dans /opt/dkp_root (cf §1) — env.sh gère ce chemin automatiquement.

# 2. Outils hôte :
sudo apt-get install -y libyaml-cpp-dev build-essential cmake
bash tools/build_host_tools.sh

# 3. Build complet :
source tools/env.sh
./build.sh
# -> dist/tricord-presence-installer.cia
```
