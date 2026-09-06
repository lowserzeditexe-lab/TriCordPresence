#pragma once
#include <stddef.h>
#include <stdbool.h>

#ifdef __3DS__
#include <3ds/result.h>
#else
typedef int Result;
#ifndef R_FAILED
#define R_FAILED(res) ((res) < 0)
#endif
#endif

/* ==========================================================================
 * tricord_account_bridge — lit le compte Discord DÉJÀ connecté dans TriCord,
 * sans jamais demander de token à l'utilisateur.
 *
 * Principe (voir docs/ACCOUNT_BRIDGE.md pour le détail) :
 *   - TriCord stocke ses comptes dans sdmc:/3ds/TriCord/accounts, un JSON
 *     {"currentIndex":N,"accounts":[{"name":...,"token":"..."} , ...]}
 *     chiffré en AES-CTR (IV nul) avec PS_KEYSLOT_0D.
 *   - PS_KEYSLOT_0D est une clé liée à LA CONSOLE (pas au compte Discord,
 *     pas à TriCord). N'importe quel process homebrew tournant sur la même
 *     3DS et ayant accès au service "ps:ps" peut appeler
 *     PS_EncryptDecryptAes avec ce keyslot — c'est exactement ce que fait
 *     TriCord lui-même dans Config::load()/save() (source/core/config.cpp).
 *   - Ce module reproduit UNIQUEMENT ce même appel système public, sur le
 *     même fichier, pour lire le token du compte actif (currentIndex).
 *     Il ne contourne aucune protection : le fichier n'est protégé que
 *     par le fait que la clé ne sort jamais de la console, propriété que
 *     ce module respecte (tout se passe on-device, rien n'est envoyé
 *     ailleurs que vers Discord via la Gateway).
 *
 * Limites connues (TODO(emergent) : à couvrir par les items du RAPPORT.md) :
 *   - Ne fonctionne pas si TriCord n'a jamais connecté de compte (fichier
 *     absent) -> accountBridgeHasAccounts() renvoie false, la gateway doit
 *     rester désactivée et réessayer périodiquement (TriCord peut se
 *     connecter après le démarrage du sysmodule).
 *   - Ne détecte pas tout seul un changement de compte actif dans TriCord
 *     pendant que le sysmodule tourne : accountBridgeGetToken() doit être
 *     rappelé périodiquement (voir discordGatewayRefreshTokenIfChanged()
 *     dans discord_gateway.h) et comparé au token en cours.
 *   - Suppose que TriCord n'a PAS le fichier ouvert en écriture au même
 *     instant (pas de verrou de fichier entre les deux process) ; un
 *     read concurrent à une sauvegarde TriCord peut échouer une fois,
 *     d'où le besoin de retry avec backoff côté appelant.
 * ========================================================================== */

#define ACCTBRIDGE_TOKEN_MAX 256

enum {
    ACCTBRIDGE_OK            = 0,
    ACCTBRIDGE_ERR_NOFILE    = -1, /* accounts absent : TriCord jamais connecté sur ce profil */
    ACCTBRIDGE_ERR_TOOBIG    = -2, /* fichier anormalement gros, probablement corrompu */
    ACCTBRIDGE_ERR_PSINIT    = -3, /* service ps:ps indisponible */
    ACCTBRIDGE_ERR_DECRYPT   = -4, /* PS_EncryptDecryptAes a échoué */
    ACCTBRIDGE_ERR_PARSE     = -5, /* JSON invalide après déchiffrement (mauvaise clé / format changé) */
    ACCTBRIDGE_ERR_NOACCOUNT = -6, /* JSON valide mais currentIndex ne pointe sur rien (déconnecté) */
};

/* À appeler une fois au démarrage du sysmodule (fait psInit() en interne). */
Result accountBridgeInit(void);
void   accountBridgeExit(void);

/* true si sdmc:/3ds/TriCord/accounts existe (sans le déchiffrer). Sert à
 * décider si on attend que l'utilisateur se connecte dans TriCord avant
 * de tenter quoi que ce soit. */
bool accountBridgeHasAccounts(void);

/* Déchiffre accounts et copie le token du compte actif (currentIndex)
 * dans out (doit faire >= ACCTBRIDGE_TOKEN_MAX octets).
 * Retourne un des ACCTBRIDGE_ERR_* ci-dessus, ou ACCTBRIDGE_OK. */
Result accountBridgeGetToken(char *out, size_t outSize);

/* Nom d'affichage du compte actif si présent dans le JSON ("" sinon).
 * Utile pour le futur HUD ("Connecté via TriCord : <name>"). */
Result accountBridgeGetAccountName(char *out, size_t outSize);
