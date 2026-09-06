#include "tricord_account_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

#ifdef __3DS__
#include <3ds.h>
#include "log.h"
#define ACB_LOG(...) logPrintf(__VA_ARGS__)
#else
#define ACB_LOG(...) do { printf("[account_bridge] " __VA_ARGS__); printf("\n"); fflush(stdout); } while (0)
#endif

/* Chemin en dur : c'est CONFIG_DIR_PATH dans TriCord/include/core/config.h.
 * TODO(emergent) si TriCord change un jour ce chemin, le resynchroniser ici. */
#define TRICORD_ACCOUNTS_PATH "sdmc:/3ds/TriCord/accounts"

/* accounts est un petit fichier (quelques comptes max) : borne large mais
 * finie pour éviter d'allouer sur un fichier corrompu/malveillant. */
#define ACB_MAX_FILE_SIZE (64 * 1024)

#ifdef __3DS__
static bool s_psInitialized = false;
#endif

Result accountBridgeInit(void) {
#ifdef __3DS__
    if (s_psInitialized) return 0;
    Result rc = psInit();
    if (R_FAILED(rc)) {
        ACB_LOG("psInit a échoué (rc=%08lX) -> bridge de compte indisponible", (unsigned long)rc);
        return ACCTBRIDGE_ERR_PSINIT;
    }
    s_psInitialized = true;
    return 0;
#else
    return 0; /* Sur hôte : rien à initialiser, accountBridgeGetToken échouera proprement. */
#endif
}

void accountBridgeExit(void) {
#ifdef __3DS__
    if (s_psInitialized) {
        psExit();
        s_psInitialized = false;
    }
#endif
}

bool accountBridgeHasAccounts(void) {
    FILE *f = fopen(TRICORD_ACCOUNTS_PATH, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

/* Lit le fichier entier. *outLen reçoit la taille lue (avant padding). */
static Result readWholeFile(const char *path, unsigned char **outBuf, size_t *outLen) {
    FILE *f = fopen(path, "rb");
    if (!f) return ACCTBRIDGE_ERR_NOFILE;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return ACCTBRIDGE_ERR_NOFILE; }
    long size = ftell(f);
    if (size < 0 || size > ACB_MAX_FILE_SIZE) { fclose(f); return ACCTBRIDGE_ERR_TOOBIG; }
    rewind(f);

    /* +1 pour un octet nul garanti après déchiffrement, cf. note dans le .h
     * (le padding AES peut tomber pile sur un multiple de 16, auquel cas
     * TriCord n'ajoute aucun octet zéro supplémentaire après le JSON). */
    size_t paddedSize = ((size_t)size + 15) & ~(size_t)15;
    unsigned char *buf = (unsigned char *)malloc(paddedSize + 1);
    if (!buf) { fclose(f); return ACCTBRIDGE_ERR_TOOBIG; }
    memset(buf, 0, paddedSize + 1);

    size_t rd = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (rd != (size_t)size) { free(buf); return ACCTBRIDGE_ERR_NOFILE; }

    *outBuf = buf;
    *outLen = paddedSize; /* taille à passer au déchiffrement, cf. encrypt_decrypt_data() côté TriCord */
    return 0;
}

/* Reproduit exactement TriCord::encrypt_decrypt_data() (source/core/config.cpp) :
 * AES-CTR, IV nul, PS_KEYSLOT_0D. CTR est symétrique : le même appel sert au
 * chiffrement et au déchiffrement, donc TriCord et nous appelons la même
 * fonction avec le même algo. */
static Result decryptInPlace(unsigned char *buf, size_t len) {
#ifdef __3DS__
    u8 iv[16];
    memset(iv, 0, sizeof(iv));
    Result rc = PS_EncryptDecryptAes(len, buf, buf, PS_ALGORITHM_CTR_ENC, PS_KEYSLOT_0D, iv);
    if (R_FAILED(rc)) {
        ACB_LOG("PS_EncryptDecryptAes a échoué (rc=%08lX)", (unsigned long)rc);
        return ACCTBRIDGE_ERR_DECRYPT;
    }
    return 0;
#else
    (void)buf; (void)len;
    /* TODO(emergent) : sur hôte, pas de service PS -> impossible de tester
     * ce chemin sans un vrai fichier accounts + une vraie console. Ajouter
     * ici un mode "fixture" optionnel (variable d'env pointant vers un
     * accounts déjà déchiffré en clair, pour tester le parsing JSON seul). */
    return ACCTBRIDGE_ERR_PSINIT;
#endif
}

/* Extrait soit le token, soit le nom, du compte à currentIndex.
 * field = "token" ou "name". */
static Result extractField(const unsigned char *plaintext, const char *field, char *out, size_t outSize) {
    json_error_t jerr;
    json_t *root = json_loads((const char *)plaintext, 0, &jerr);
    if (!root) {
        ACB_LOG("JSON invalide après déchiffrement (%s @ %d) -> mauvais keyslot ou format TriCord modifié",
                jerr.text, jerr.position);
        return ACCTBRIDGE_ERR_PARSE;
    }

    Result result = ACCTBRIDGE_ERR_NOACCOUNT;
    json_t *jIndex = json_object_get(root, "currentIndex");
    json_t *jAccounts = json_object_get(root, "accounts");
    if (json_is_integer(jIndex) && json_is_array(jAccounts)) {
        size_t idx = (size_t)json_integer_value(jIndex);
        json_t *jAcc = json_array_get(jAccounts, idx);
        if (json_is_object(jAcc)) {
            json_t *jField = json_object_get(jAcc, field);
            if (json_is_string(jField)) {
                snprintf(out, outSize, "%s", json_string_value(jField));
                result = 0;
            }
        }
    }

    json_decref(root);
    return result;
}

Result accountBridgeGetToken(char *out, size_t outSize) {
    if (outSize < ACCTBRIDGE_TOKEN_MAX) return ACCTBRIDGE_ERR_TOOBIG;
    out[0] = '\0';

    unsigned char *buf = NULL;
    size_t len = 0;
    Result rc = readWholeFile(TRICORD_ACCOUNTS_PATH, &buf, &len);
    if (R_FAILED(rc)) return rc;

    rc = decryptInPlace(buf, len);
    if (R_FAILED(rc)) { free(buf); return rc; }

    rc = extractField(buf, "token", out, outSize);
    free(buf);

    if (R_FAILED(rc)) {
        ACB_LOG("Aucun compte actif dans accounts (currentIndex invalide ou déconnecté)");
    }
    return rc;
}

Result accountBridgeGetAccountName(char *out, size_t outSize) {
    out[0] = '\0';

    unsigned char *buf = NULL;
    size_t len = 0;
    Result rc = readWholeFile(TRICORD_ACCOUNTS_PATH, &buf, &len);
    if (R_FAILED(rc)) return rc;

    rc = decryptInPlace(buf, len);
    if (R_FAILED(rc)) { free(buf); return rc; }

    rc = extractField(buf, "name", out, outSize);
    free(buf);
    return rc;
}
