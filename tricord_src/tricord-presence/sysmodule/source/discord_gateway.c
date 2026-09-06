#include "discord_gateway.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/base64.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/x509_crt.h>
#include <wslay/wslay.h>
#include "discord_ca_bundle.h"
#include "tricord_account_bridge.h"
#include <jansson.h>

/* ------------------------------------------------------------------------
 * Couche plateforme : 3DS (libctru) ou hôte Linux (test sans console).
 * ---------------------------------------------------------------------- */
#ifdef __3DS__
#include <3ds.h>
#include <malloc.h>
#include "log.h"
#define GW_CONFIG_PATH "sdmc:/3ds/tricord-presence/config.txt"
typedef LightLock gw_mutex_t;
static void gw_mutex_init(gw_mutex_t *m)   { LightLock_Init(m); }
static void gw_mutex_lock(gw_mutex_t *m)   { LightLock_Lock(m); }
static void gw_mutex_unlock(gw_mutex_t *m) { LightLock_Unlock(m); }
static void gw_sleep_ms(u32 ms) { svcSleepThread((u64)ms * 1000000ULL); }
static u64  gw_now_ms(void) { return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000); }
#define GW_LOG(...) logPrintf(__VA_ARGS__)
#else
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#define GW_CONFIG_PATH (getenv("TRICORD_CONFIG") ? getenv("TRICORD_CONFIG") : "config.txt")
typedef pthread_mutex_t gw_mutex_t;
static void gw_mutex_init(gw_mutex_t *m)   { pthread_mutex_init(m, NULL); }
static void gw_mutex_lock(gw_mutex_t *m)   { pthread_mutex_lock(m); }
static void gw_mutex_unlock(gw_mutex_t *m) { pthread_mutex_unlock(m); }
static void gw_sleep_ms(u32 ms) { usleep(ms * 1000); }
static u64  gw_now_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (u64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000; }

#define GW_LOG(...) do { printf("[gateway] " __VA_ARGS__); printf("\n"); fflush(stdout); } while (0)
#endif

/* Callback de vérification X.509 tolérant à l'horloge (code commun 3DS/hôte).
 *
 * BUG HARDWARE CONFIRMÉ (log.txt console) : le handshake TLS vers
 * gateway.discord.gg échouait avec -0x2700 (MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
 * et "The certificate validity starts in the future" — l'horloge RTC de la 3DS
 * est en retard sur la date de validité du certificat Discord. La RTC d'une 3DS
 * n'est pas fiable/synchronisée : on NE PEUT PAS s'en servir pour valider les
 * dates TLS. On efface donc UNIQUEMENT les bits de validité temporelle
 * (BADCERT_FUTURE / BADCERT_EXPIRED) ; la chaîne de confiance (CA embarquées)
 * et le nom d'hôte (SNI/CN) restent vérifiés normalement. Approche standard des
 * clients TLS homebrew 3DS (RTC non fiable). Reste TODO(hw) : idéalement
 * resynchroniser l'horloge, mais le pont ne doit pas dépendre de la RTC. */
static int tls_verify_cb(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
    (void)ctx; (void)crt; (void)depth;
    *flags &= ~(uint32_t)(MBEDTLS_X509_BADCERT_FUTURE | MBEDTLS_X509_BADCERT_EXPIRED);
    return 0;
}


#define GW_DEFAULT_HOST   "gateway.discord.gg"
#define GW_PATH           "/?v=10&encoding=json"
#define GW_PORT           "443"
/* Un message est accumulé en RAM jusqu'à GW_ACC_MAX ; au-delà (READY d'un
 * compte utilisateur = plusieurs MiB : guilds, DMs...) on bascule en mode
 * "scan" : les chunks WebSocket sont parcourus à la volée pour extraire
 * session_id / resume_gateway_url sans jamais stocker le message. */
#define GW_ACC_MAX        (96 * 1024)
#define GW_SCAN_RING      32
#define GW_PRESENCE_MIN_INTERVAL_MS 15000 /* Discord : ~5 updates / minute max */

/* ------------------------------------------------------------------------
 * État global du client
 * ---------------------------------------------------------------------- */
typedef struct {
    int fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    wslay_event_context_ptr ws;

    /* état de session Discord */
    char session_id[128];
    char resume_host[128];
    long long seq;
    bool has_seq;
    bool ready;              /* READY/RESUMED reçu -> on peut envoyer op 3 */
    bool hello_received;
    u32  heartbeat_interval_ms;
    u64  next_heartbeat_at;
    bool heartbeat_acked;
    bool want_close;         /* fermer la connexion courante */
    bool resume_on_reconnect;
    bool fatal;              /* ne plus jamais se reconnecter */
    int  close_code;
    bool invalid_session_wait;

    /* réception sans buffering wslay (cf GW_ACC_MAX) */
    uint8_t *acc;
    size_t   acc_len;
    bool     acc_overflow;
    bool     acc_is_text;
    uint8_t  ring[GW_SCAN_RING];
    size_t   ring_pos;
    int      scan_collect;      /* -1 = aucun, sinon index de la clé collectée */
    int      scan_depth;
    bool     scan_in_string;
    bool     scan_escape;
    size_t   scan_out_len;
    char     scan_session_id[128];
    char     scan_resume_url[128];
} gw_t;

static char s_token[256];
static gw_t s_gw;
static volatile bool s_threadRunning = false;

static gw_mutex_t s_presenceLock;
static presence_state_t s_presence;
static bool s_presenceValid = false;
static bool s_presenceDirty = false;
static u64  s_lastPresenceSentAt = 0;

#ifdef __3DS__
static Thread s_thread;
static u32 *s_socBuffer = NULL;
#define SOC_BUFFERSIZE 0x100000
#else
static pthread_t s_thread;
#endif

/* ------------------------------------------------------------------------
 * Source du token, par ordre de priorité :
 *
 *   1) tricord_account_bridge : compte déjà connecté dans TriCord
 *      (sdmc:/3ds/TriCord/accounts, déchiffré via PS_KEYSLOT_0D). C'est le
 *      chemin nominal — aucune saisie de token par l'utilisateur.
 *
 *   2) config.txt (DEV/TEST UNIQUEMENT) : n'existe que pour les tests hôte
 *      (tools/host_gateway_test, où psInit/PS_KEYSLOT_0D n'existent pas) et
 *      pour un dépannage manuel. Ne JAMAIS documenter cette voie comme le
 *      fonctionnement normal dans l'installeur/README destinés aux
 *      utilisateurs finaux.
 * ---------------------------------------------------------------------- */
static Result loadTokenFromConfig(char *out, size_t outSize) {
    FILE *f = fopen(GW_CONFIG_PATH, "r");
    if (!f) return -1;
    char line[256];
    out[0] = '\0';
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ')) line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;
        const char *val = line;
        if (strncmp(line, "token=", 6) == 0) val = line + 6;
        else if (strchr(line, '=')) continue; /* autre clé de config */
        snprintf(out, outSize, "%s", val);
        break;
    }
    fclose(f);
    return out[0] ? 0 : -1;
}

/* Choisit la source du token pour l'IDENTIFY initial. Voir commentaire
 * ci-dessus pour l'ordre de priorité. */
static Result loadToken(char *out, size_t outSize) {
    if (accountBridgeHasAccounts()) {
        Result rc = accountBridgeGetToken(out, outSize);
        if (!R_FAILED(rc)) {
            GW_LOG("Token récupéré depuis le compte TriCord actif (aucune saisie utilisateur)");
            return 0;
        }
        GW_LOG("accounts TriCord présent mais illisible (rc=%d) -> tentative config.txt", rc);
    } else {
        GW_LOG("Aucun compte TriCord trouvé (accounts absent) -> tentative config.txt");
    }
    return loadTokenFromConfig(out, outSize);
}

/* ------------------------------------------------------------------------
 * Socket + TLS
 * ---------------------------------------------------------------------- */
static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    ssize_t r = send(fd, buf, len, 0);
    if (r < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) return MBEDTLS_ERR_SSL_WANT_WRITE;
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return (int)r;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    ssize_t r = recv(fd, buf, len, 0);
    if (r < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) return MBEDTLS_ERR_SSL_WANT_READ;
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return (int)r;
}

static int wait_fd(int fd, bool for_write, int timeout_ms) {
    struct pollfd p = { .fd = fd, .events = (short)(for_write ? POLLOUT : POLLIN) };
    return poll(&p, 1, timeout_ms);
}

static int tcp_connect(const char *host) {
    struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr_list[0]) { GW_LOG("DNS échoué pour %s", host); return -1; }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(443);
    memcpy(&sa.sin_addr, he->h_addr_list[0], sizeof(sa.sin_addr));

    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return fd;
}

static bool tls_connect(gw_t *g, const char *host) {
    mbedtls_ssl_init(&g->ssl);
    mbedtls_ssl_config_init(&g->conf);
    mbedtls_x509_crt_init(&g->cacert);
    mbedtls_entropy_init(&g->entropy);
    mbedtls_ctr_drbg_init(&g->drbg);

    const char *pers = "tricord_presenced";
    if (mbedtls_ctr_drbg_seed(&g->drbg, mbedtls_entropy_func, &g->entropy,
                              (const unsigned char *)pers, strlen(pers)) != 0) {
        GW_LOG("ctr_drbg_seed échoué"); return false;
    }
    if (mbedtls_ssl_config_defaults(&g->conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        return false;
    /* Vérification du certificat serveur contre les racines embarquées
     * (discord_ca_bundle.h, généré par tools/gen_ca_bundle.py) + contrôle du
     * nom d'hôte (mbedtls_ssl_set_hostname ci-dessous). La 3DS n'expose pas
     * son magasin de CA à mbedtls, d'où l'embarquement.
     * TODO(hardware): mbedtls compare les dates de validité à time() (RTC de
     * la console) : une horloge 3DS très fausse fait échouer la connexion
     * (erreur MBEDTLS_X509_BADCERT_EXPIRED/FUTURE dans log.txt). */
    int cr = mbedtls_x509_crt_parse(&g->cacert, (const unsigned char *)DISCORD_CA_BUNDLE, sizeof(DISCORD_CA_BUNDLE));
    if (cr < 0) { GW_LOG("bundle CA invalide: -0x%04X", (unsigned)-cr); return false; }
    if (cr > 0) GW_LOG("bundle CA: %d certificat(s) ignoré(s)", cr);
    mbedtls_ssl_conf_ca_chain(&g->conf, &g->cacert, NULL);
    mbedtls_ssl_conf_authmode(&g->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    /* Tolérance à l'horloge RTC (voir tls_verify_cb) : la chaîne CA et le nom
     * d'hôte restent vérifiés, seules les dates de validité sont ignorées. */
    mbedtls_ssl_conf_verify(&g->conf, tls_verify_cb, NULL);
    mbedtls_ssl_conf_rng(&g->conf, mbedtls_ctr_drbg_random, &g->drbg);
    mbedtls_ssl_conf_min_version(&g->conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);

    if (mbedtls_ssl_setup(&g->ssl, &g->conf) != 0) return false;
    mbedtls_ssl_set_hostname(&g->ssl, host); /* SNI obligatoire (Cloudflare) */
    mbedtls_ssl_set_bio(&g->ssl, &g->fd, bio_send, bio_recv, NULL);

    u64 deadline = gw_now_ms() + 20000;
    int ret;
    while ((ret = mbedtls_ssl_handshake(&g->ssl)) != 0) {
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (gw_now_ms() > deadline) { GW_LOG("TLS handshake timeout"); return false; }
            wait_fd(g->fd, ret == MBEDTLS_ERR_SSL_WANT_WRITE, 1000);
            continue;
        }
        GW_LOG("TLS handshake échoué: -0x%04X", (unsigned)-ret);
        if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
            char vbuf[256];
            mbedtls_x509_crt_verify_info(vbuf, sizeof(vbuf), "  ", mbedtls_ssl_get_verify_result(&g->ssl));
            GW_LOG("certificat refusé:\n%s", vbuf);
        }
        return false;
    }
    return true;
}

static void tls_free(gw_t *g) {
    mbedtls_ssl_close_notify(&g->ssl);
    mbedtls_ssl_free(&g->ssl);
    mbedtls_ssl_config_free(&g->conf);
    mbedtls_x509_crt_free(&g->cacert);
    mbedtls_ctr_drbg_free(&g->drbg);
    mbedtls_entropy_free(&g->entropy);
}

static bool tls_write_all(gw_t *g, const unsigned char *buf, size_t len) {
    u64 deadline = gw_now_ms() + 10000;
    while (len > 0) {
        int r = mbedtls_ssl_write(&g->ssl, buf, len);
        if (r > 0) { buf += r; len -= (size_t)r; continue; }
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (gw_now_ms() > deadline) return false;
            wait_fd(g->fd, r == MBEDTLS_ERR_SSL_WANT_WRITE, 1000);
            continue;
        }
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------
 * Handshake HTTP -> WebSocket (RFC 6455 §4.1)
 * ---------------------------------------------------------------------- */
static bool ws_handshake(gw_t *g, const char *host) {
    unsigned char nonce[16];
    unsigned char key_b64[32];
    size_t olen = 0;
    mbedtls_ctr_drbg_random(&g->drbg, nonce, sizeof(nonce));
    mbedtls_base64_encode(key_b64, sizeof(key_b64), &olen, nonce, sizeof(nonce));
    key_b64[olen] = '\0';

    char req[512];
    int n = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "User-Agent: TriCordPresence/0.1 (Nintendo 3DS)\r\n"
        "\r\n", GW_PATH, host, key_b64);
    if (!tls_write_all(g, (const unsigned char *)req, (size_t)n)) return false;

    /* Lire l'en-tête de réponse jusqu'à la ligne vide. Le serveur n'envoie
     * rien d'autre avant HELLO, qui arrive dans une trame WS séparée : on
     * peut donc s'arrêter exactement à "\r\n\r\n" sans perdre de données
     * (lecture octet par octet pour ne pas consommer la première trame). */
    char resp[2048];
    size_t got = 0;
    u64 deadline = gw_now_ms() + 10000;
    while (got < sizeof(resp) - 1) {
        int r = mbedtls_ssl_read(&g->ssl, (unsigned char *)resp + got, 1);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (gw_now_ms() > deadline) { GW_LOG("WS handshake timeout"); return false; }
            wait_fd(g->fd, false, 1000);
            continue;
        }
        if (r <= 0) return false;
        got += (size_t)r;
        if (got >= 4 && memcmp(resp + got - 4, "\r\n\r\n", 4) == 0) break;
    }
    resp[got] = '\0';
    if (strncmp(resp, "HTTP/1.1 101", 12) != 0) {
        GW_LOG("WS upgrade refusé: %.40s", resp);
        return false;
    }
    /* TODO(hardware): vérifier Sec-WebSocket-Accept (SHA1+base64 de la clé) —
     * optionnel côté client, omis pour l'instant. */
    return true;
}

/* ------------------------------------------------------------------------
 * Callbacks wslay
 * ---------------------------------------------------------------------- */
static ssize_t ws_recv_cb(wslay_event_context_ptr ctx, uint8_t *buf, size_t len, int flags, void *ud) {
    (void)flags;
    gw_t *g = (gw_t *)ud;
    int r = mbedtls_ssl_read(&g->ssl, buf, len);
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
        wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
        return -1;
    }
    if (r <= 0) { wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE); return -1; }
    return r;
}

static ssize_t ws_send_cb(wslay_event_context_ptr ctx, const uint8_t *data, size_t len, int flags, void *ud) {
    (void)flags;
    gw_t *g = (gw_t *)ud;
    int r = mbedtls_ssl_write(&g->ssl, data, len);
    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
        wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
        return -1;
    }
    if (r < 0) { wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE); return -1; }
    return r;
}

static int ws_genmask_cb(wslay_event_context_ptr ctx, uint8_t *buf, size_t len, void *ud) {
    (void)ctx;
    gw_t *g = (gw_t *)ud;
    return mbedtls_ctr_drbg_random(&g->drbg, buf, len) == 0 ? 0 : -1;
}


/* ------------------------------------------------------------------------
 * Scan à la volée d'un message trop gros pour être bufferisé
 * ---------------------------------------------------------------------- */
typedef struct { const char *needle; size_t len; } scan_key_t;
static const scan_key_t s_scanKeys[2] = {
    { "\"session_id\":\"", 14 },
    { "\"resume_gateway_url\":\"", 22 },
};

static char *scan_out(gw_t *g, int key) {
    return key == 0 ? g->scan_session_id : g->scan_resume_url;
}

static bool ring_ends_with(const gw_t *g, const char *needle, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (g->ring[(g->ring_pos - n + i) % GW_SCAN_RING] != (uint8_t)needle[i]) return false;
    return true;
}

static void scan_reset(gw_t *g) {
    g->ring_pos = GW_SCAN_RING; /* évite les modulo négatifs */
    memset(g->ring, 0, sizeof(g->ring));
    g->scan_collect = -1;
    g->scan_depth = 0;
    g->scan_in_string = false;
    g->scan_escape = false;
    g->scan_session_id[0] = g->scan_resume_url[0] = '\0';
}

/* Suit la profondeur JSON pour n'accepter les clés qu'au niveau de "d"
 * (profondeur 2) : le READY d'un compte utilisateur contient aussi
 * "sessions":[{"session_id":...}] à une profondeur supérieure. */
static void scan_feed(gw_t *g, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (g->scan_collect >= 0) {
            char *out = scan_out(g, g->scan_collect);
            if (g->scan_escape) { g->scan_escape = false; if (g->scan_out_len < 127) out[g->scan_out_len++] = (char)c; }
            else if (c == '\\') g->scan_escape = true;
            else if (c == '"' || g->scan_out_len >= 127) { out[g->scan_out_len] = '\0'; g->scan_collect = -1; g->scan_in_string = false; }
            else out[g->scan_out_len++] = (char)c;
            continue;
        }
        g->ring[g->ring_pos++ % GW_SCAN_RING] = c;
        if (g->scan_in_string) {
            if (g->scan_escape) g->scan_escape = false;
            else if (c == '\\') g->scan_escape = true;
            else if (c == '"') g->scan_in_string = false;
            continue;
        }
        if (c == '{' || c == '[') g->scan_depth++;
        else if (c == '}' || c == ']') g->scan_depth--;
        else if (c == '"') {
            g->scan_in_string = true;
            if (g->scan_depth != 2) continue;
            for (int k = 0; k < 2; k++) {
                if (scan_out(g, k)[0] == '\0' && ring_ends_with(g, s_scanKeys[k].needle, s_scanKeys[k].len)) {
                    g->scan_collect = k;
                    g->scan_out_len = 0;
                    break;
                }
            }
        }
    }
}

static void ws_on_frame_start_cb(wslay_event_context_ptr ctx, const struct wslay_event_on_frame_recv_start_arg *arg, void *ud) {
    (void)ctx;
    gw_t *g = (gw_t *)ud;
    if (arg->opcode == WSLAY_TEXT_FRAME) { /* début d'un nouveau message (pas une continuation) */
        g->acc_len = 0;
        g->acc_overflow = false;
        g->acc_is_text = true;
        scan_reset(g);
    } else if (arg->opcode != 0 /* continuation */) {
        g->acc_is_text = false;
    }
}

static void ws_on_frame_chunk_cb(wslay_event_context_ptr ctx, const struct wslay_event_on_frame_recv_chunk_arg *arg, void *ud) {
    (void)ctx;
    gw_t *g = (gw_t *)ud;
    if (!g->acc_is_text) return;
    if (!g->acc_overflow && g->acc_len + arg->data_length <= GW_ACC_MAX) {
        memcpy(g->acc + g->acc_len, arg->data, arg->data_length);
        g->acc_len += arg->data_length;
        return;
    }
    if (!g->acc_overflow) { /* bascule : rejouer ce qui était accumulé dans le scanner */
        g->acc_overflow = true;
        scan_feed(g, g->acc, g->acc_len);
        g->acc_len = 0;
    }
    scan_feed(g, arg->data, arg->data_length);
}

static void handle_large_dispatch(gw_t *g);

static void handle_gateway_event(gw_t *g, const uint8_t *msg, size_t len);

static void ws_on_msg_cb(wslay_event_context_ptr ctx, const struct wslay_event_on_msg_recv_arg *arg, void *ud) {
    (void)ctx;
    gw_t *g = (gw_t *)ud;
    if (arg->opcode == WSLAY_TEXT_FRAME) {
        /* buffering wslay désactivé : le message est dans g->acc (ou a été scanné) */
        if (!g->acc_overflow) handle_gateway_event(g, g->acc, g->acc_len);
        else handle_large_dispatch(g);
        g->acc_len = 0;
    } else if (arg->opcode == WSLAY_CONNECTION_CLOSE) {
        g->close_code = arg->status_code;
        g->want_close = true;
        GW_LOG("close reçu, code %d", arg->status_code);
        /* https://discord.com/developers/docs/topics/opcodes-and-status-codes#gateway-gateway-close-event-codes */
        switch (arg->status_code) {
        case 4004: GW_LOG("Authentification refusée (4004) : token invalide -> arrêt"); g->fatal = true; break;
        case 4010: case 4011: case 4012: case 4013: case 4014: g->fatal = true; break;
        case 4007: case 4009: g->resume_on_reconnect = false; break; /* seq/session invalide -> re-IDENTIFY */
        default: g->resume_on_reconnect = true; break;
        }
    }
}

/* ------------------------------------------------------------------------
 * Envoi de payloads
 * ---------------------------------------------------------------------- */
static bool send_json(gw_t *g, json_t *root) {
    char *txt = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    if (!txt) return false;
    struct wslay_event_msg m = { WSLAY_TEXT_FRAME, (const uint8_t *)txt, strlen(txt) };
    int rc = wslay_event_queue_msg(g->ws, &m);
    free(txt);
    return rc == 0;
}

#define GW_IDLE_TEXT "Sur le menu HOME"

/* Payload "d" d'Update Presence (op 3) :
 *  - en jeu   : status "online" + activité type 0 -> "Joue à <jeu>"
 *  - menu HOME: status "idle" + statut personnalisé (type 4, champ "state",
 *    seul type de texte libre affiché pour un compte utilisateur) ->
 *    "Sur le menu HOME". Remplace le statut perso de l'utilisateur tant que
 *    le sysmodule tourne (voir RAPPORT.md).
 *  - inconnu  : status "online", aucune activité. */
static json_t *build_presence_d(const presence_state_t *st) {
    json_t *activities = json_array();
    const char *status = "online";
    if (st && st->kind == PRESENCE_KIND_IN_GAME) {
        json_array_append_new(activities, json_pack("{s:s, s:i}", "name", st->game_name, "type", 0));
    } else if (st && st->kind == PRESENCE_KIND_IDLE) {
        status = "idle";
        json_array_append_new(activities, json_pack("{s:s, s:i, s:s}", "name", "Custom Status", "type", 4, "state", GW_IDLE_TEXT));
    }
    return json_pack("{s:n, s:o, s:s, s:b}", "since", "activities", activities, "status", status, "afk", 0);
}

static void send_heartbeat(gw_t *g) {
    json_t *d = g->has_seq ? json_integer(g->seq) : json_null();
    send_json(g, json_pack("{s:i, s:o}", "op", 1, "d", d));
    g->heartbeat_acked = false;
    g->next_heartbeat_at = gw_now_ms() + g->heartbeat_interval_ms;
}

static void send_identify(gw_t *g) {
    presence_state_t st;
    bool valid;
    gw_mutex_lock(&s_presenceLock);
    st = s_presence; valid = s_presenceValid;
    gw_mutex_unlock(&s_presenceLock);

    /* Token utilisateur : pas de champ "intents" (réservé aux bots), les
     * "properties" imitent un client (os/browser/device). */
    json_t *d = json_pack("{s:s, s:{s:s, s:s, s:s}, s:b, s:o}",
                          "token", s_token,
                          "properties", "os", "Nintendo 3DS", "browser", "TriCord", "device", "Nintendo 3DS",
                          "compress", 0,
                          "presence", build_presence_d(valid ? &st : NULL));
    send_json(g, json_pack("{s:i, s:o}", "op", 2, "d", d));
    GW_LOG("IDENTIFY envoyé");
}

static void send_resume(gw_t *g) {
    send_json(g, json_pack("{s:i, s:{s:s, s:s, s:I}}", "op", 6, "d",
                           "token", s_token, "session_id", g->session_id, "seq", (json_int_t)g->seq));
    GW_LOG("RESUME envoyé (seq=%lld)", g->seq);
}

static void send_presence_update(gw_t *g, const presence_state_t *st) {
    send_json(g, json_pack("{s:i, s:o}", "op", 3, "d", build_presence_d(st)));
    s_lastPresenceSentAt = gw_now_ms();
    GW_LOG("UPDATE PRESENCE envoyé (%s)", st->kind == PRESENCE_KIND_IN_GAME ? st->game_name :
                                          st->kind == PRESENCE_KIND_IDLE ? "idle: " GW_IDLE_TEXT : "unknown");
}

static void on_ready(gw_t *g, const char *sid, const char *rurl) {
    if (sid) snprintf(g->session_id, sizeof(g->session_id), "%s", sid);
    if (rurl) {
        if (strncmp(rurl, "wss://", 6) == 0) rurl += 6;
        snprintf(g->resume_host, sizeof(g->resume_host), "%s", rurl);
        char *slash = strchr(g->resume_host, '/');
        if (slash) *slash = '\0';
    }
    g->ready = true;
    g->resume_on_reconnect = true;
    GW_LOG("READY (session %s, resume via %s)", g->session_id, g->resume_host);
    gw_mutex_lock(&s_presenceLock);
    s_presenceDirty = s_presenceValid; /* republier l'état courant */
    gw_mutex_unlock(&s_presenceLock);
}

/* Message > GW_ACC_MAX : en pratique READY (ou GUILD_CREATE, ignoré). Si le
 * scanner a trouvé un session_id, c'est un READY. */
static void handle_large_dispatch(gw_t *g) {
    if (g->scan_session_id[0]) {
        GW_LOG("READY volumineux traité en streaming");
        on_ready(g, g->scan_session_id, g->scan_resume_url[0] ? g->scan_resume_url : NULL);
    } else {
        GW_LOG("dispatch volumineux ignoré");
    }
}

/* ------------------------------------------------------------------------
 * Réception : dispatch des opcodes
 * https://discord.com/developers/docs/topics/opcodes-and-status-codes#gateway-gateway-opcodes
 * ---------------------------------------------------------------------- */
static void handle_gateway_event(gw_t *g, const uint8_t *msg, size_t len) {
    json_error_t err;
    json_t *root = json_loadb((const char *)msg, len, 0, &err);
    if (!root) { GW_LOG("JSON invalide: %s", err.text); return; }

    int op = (int)json_integer_value(json_object_get(root, "op"));
    json_t *d = json_object_get(root, "d");
    json_t *s = json_object_get(root, "s");
    if (json_is_integer(s)) { g->seq = json_integer_value(s); g->has_seq = true; }

    switch (op) {
    case 10: { /* HELLO */
        g->heartbeat_interval_ms = (u32)json_integer_value(json_object_get(d, "heartbeat_interval"));
        if (g->heartbeat_interval_ms < 1000) g->heartbeat_interval_ms = 41250;
        g->hello_received = true;
        g->heartbeat_acked = true;
        /* premier heartbeat après interval * jitter (jitter ∈ [0,1[) */
        unsigned char jb;
        mbedtls_ctr_drbg_random(&g->drbg, &jb, 1);
        g->next_heartbeat_at = gw_now_ms() + (u64)g->heartbeat_interval_ms * jb / 256;
        GW_LOG("HELLO: heartbeat_interval=%u ms", (unsigned)g->heartbeat_interval_ms);
        if (g->resume_on_reconnect && g->session_id[0] && g->has_seq) send_resume(g);
        else send_identify(g);
        break;
    }
    case 11: /* HEARTBEAT ACK */
        g->heartbeat_acked = true;
        break;
    case 1:  /* le serveur demande un heartbeat immédiat */
        send_heartbeat(g);
        break;
    case 0: { /* DISPATCH */
        const char *t = json_string_value(json_object_get(root, "t"));
        if (t && strcmp(t, "READY") == 0) {
            on_ready(g, json_string_value(json_object_get(d, "session_id")),
                        json_string_value(json_object_get(d, "resume_gateway_url")));
        } else if (t && strcmp(t, "RESUMED") == 0) {
            g->ready = true;
            GW_LOG("RESUMED");
        }
        break;
    }
    case 7: /* RECONNECT */
        GW_LOG("RECONNECT demandé");
        g->resume_on_reconnect = true;
        g->want_close = true;
        break;
    case 9: /* INVALID SESSION : d = true si reprise possible */
        GW_LOG("INVALID SESSION (resumable=%d)", json_is_true(d) ? 1 : 0);
        g->resume_on_reconnect = json_is_true(d);
        if (!g->resume_on_reconnect) { g->session_id[0] = '\0'; g->has_seq = false; }
        g->invalid_session_wait = true;
        g->want_close = true;
        break;
    default:
        break;
    }
    json_decref(root);
}

/* ------------------------------------------------------------------------
 * Une session complète : connect -> handshake -> boucle événementielle
 * ---------------------------------------------------------------------- */
static void run_session(gw_t *g) {
    const char *host = (g->resume_on_reconnect && g->resume_host[0]) ? g->resume_host : GW_DEFAULT_HOST;
#ifndef __3DS__
    if (getenv("TRICORD_GW_HOST")) host = getenv("TRICORD_GW_HOST"); /* test hôte : hôte hors bundle CA */
#endif
    GW_LOG("connexion à %s", host);

    g->fd = tcp_connect(host);
    if (g->fd < 0) { GW_LOG("TCP connect échoué"); g->resume_on_reconnect = g->session_id[0] != 0; return; }
    if (!tls_connect(g, host)) { tls_free(g); close(g->fd); return; }
    if (!ws_handshake(g, host)) { tls_free(g); close(g->fd); return; }

    struct wslay_event_callbacks cbs = {
        ws_recv_cb, ws_send_cb, ws_genmask_cb, ws_on_frame_start_cb, ws_on_frame_chunk_cb, NULL, ws_on_msg_cb
    };
    if (wslay_event_context_client_init(&g->ws, &cbs, g) != 0) { tls_free(g); close(g->fd); return; }
    wslay_event_config_set_no_buffering(g->ws, 1); /* cf GW_ACC_MAX */
    g->acc_len = 0; g->acc_overflow = false; g->acc_is_text = false;
    scan_reset(g);

    g->hello_received = false;
    g->ready = false;
    g->want_close = false;
    g->close_code = 0;
    u64 hello_deadline = gw_now_ms() + 15000;

    while (!g->want_close && wslay_event_want_read(g->ws)) {
        struct pollfd p = { .fd = g->fd, .events = POLLIN };
        if (wslay_event_want_write(g->ws)) p.events |= POLLOUT;
        int pr = poll(&p, 1, 500);
        if (pr < 0) { GW_LOG("poll erreur"); break; }
        if (pr > 0) {
            if ((p.revents & (POLLIN | POLLERR | POLLHUP)) && wslay_event_recv(g->ws) != 0) { GW_LOG("wslay recv erreur"); break; }
            if ((p.revents & POLLOUT) && wslay_event_send(g->ws) != 0) { GW_LOG("wslay send erreur"); break; }
        }
        /* mbedtls peut avoir bufferisé des octets déjà déchiffrés */
        if (mbedtls_ssl_get_bytes_avail(&g->ssl) > 0 && wslay_event_recv(g->ws) != 0) break;

        u64 now = gw_now_ms();
        if (!g->hello_received) {
            if (now > hello_deadline) { GW_LOG("pas de HELLO -> reconnexion"); break; }
            continue;
        }
        if (now >= g->next_heartbeat_at) {
            if (!g->heartbeat_acked) { /* connexion zombie */
                GW_LOG("heartbeat non acquitté -> reconnexion (RESUME)");
                g->resume_on_reconnect = true;
                break;
            }
            send_heartbeat(g);
        }
        if (g->ready) {
            gw_mutex_lock(&s_presenceLock);
            bool dirty = s_presenceDirty;
            presence_state_t st = s_presence;
            if (dirty && now - s_lastPresenceSentAt >= GW_PRESENCE_MIN_INTERVAL_MS) s_presenceDirty = false;
            else dirty = false;
            gw_mutex_unlock(&s_presenceLock);
            if (dirty) send_presence_update(g, &st);
        }
        if (wslay_event_want_write(g->ws)) wslay_event_send(g->ws);
    }

    if (!wslay_event_get_close_sent(g->ws)) {
        /* 1000 = fermeture normale ; la session reste reprenable côté Discord
         * uniquement si on ferme avec un code != 1000/1001 */
        wslay_event_queue_close(g->ws, g->resume_on_reconnect ? 4000 : 1000, NULL, 0);
        wslay_event_send(g->ws);
    }
    g->ready = false;
    wslay_event_context_free(g->ws);
    g->ws = NULL;
    tls_free(g);
    close(g->fd);
    g->fd = -1;
}

static void gateway_thread_main(void *arg) {
    (void)arg;
    gw_t *g = &s_gw;
    u32 backoff_ms = 5000;

#ifdef __3DS__
    /* Attendre une connexion Internet (ac:u) avant la première tentative. */
    if (R_SUCCEEDED(acInit())) {
        u32 status = 0;
        for (int i = 0; i < 120 && s_threadRunning; i++) {
            if (R_SUCCEEDED(ACU_GetStatus(&status)) && status == 3) break;
            gw_sleep_ms(1000);
        }
        acExit();
    }
    /* Comme Rosalina (minisoc.c) : rester en mode infrastructure en fond. */
    if (R_SUCCEEDED(ndmuInit())) {
        NDMU_EnterExclusiveState(NDM_EXCLUSIVE_STATE_INFRASTRUCTURE);
    }
#endif

    while (s_threadRunning && !g->fatal) {
        u64 start = gw_now_ms();
        run_session(g);
        if (g->fatal || !s_threadRunning) break;

        if (g->invalid_session_wait) { /* Discord : attendre 1 à 5 s avant re-IDENTIFY */
            g->invalid_session_wait = false;
            gw_sleep_ms(3000);
            continue;
        }
        /* session longue (> 1 min) = reconnexion "normale", backoff remis à zéro */
        if (gw_now_ms() - start > 60000) backoff_ms = 5000;
        GW_LOG("reconnexion dans %u ms", (unsigned)backoff_ms);
        gw_sleep_ms(backoff_ms);
        if (backoff_ms < 60000) backoff_ms *= 2;
    }
#ifdef __3DS__
    NDMU_LeaveExclusiveState();
    ndmuExit();
#endif
    GW_LOG("thread gateway terminé (fatal=%d)", g->fatal ? 1 : 0);
}

#ifndef __3DS__
static void *gateway_thread_trampoline(void *arg) { gateway_thread_main(arg); return NULL; }
#endif

/* ------------------------------------------------------------------------
 * API publique
 * ---------------------------------------------------------------------- */
Result discordGatewayInit(void) {
    memset(&s_gw, 0, sizeof(s_gw));
    s_gw.fd = -1;
    gw_mutex_init(&s_presenceLock);

    Result rc = accountBridgeInit();
    if (R_FAILED(rc)) {
        GW_LOG("accountBridgeInit a échoué (rc=%d) -> repli sur config.txt uniquement", rc);
    }

    rc = loadToken(s_token, sizeof(s_token));
    if (R_FAILED(rc)) {
        GW_LOG("Aucun token disponible (ni TriCord ni config.txt) -> gateway désactivée");
        /* TODO(emergent) : ne pas juste échouer ici. Le sysmodule doit rester
         * en vie et réessayer périodiquement (ex. toutes les 10s) car
         * l'utilisateur peut se connecter dans TriCord APRÈS le démarrage
         * du sysmodule (boot Luma -> lancement du sysmodule -> l'utilisateur
         * lance TriCord et se log seulement ensuite). Voir main.c : boucle
         * de "retry connect" à ajouter avant d'appeler discordGatewayInit
         * une deuxième fois, plutôt que de renvoyer une erreur fatale. */
        return rc;
    }
    s_gw.acc = (uint8_t *)malloc(GW_ACC_MAX);
    if (!s_gw.acc) return -2;

#ifdef __3DS__
    /* Robustesse : chaque chemin d'échec libère ce qui a été alloué pour que
     * les réessais périodiques (main.c, mode dégradé) ne fuient pas de
     * mémoire. Le sysmodule reste vivant et retentera plus tard. */
    s_socBuffer = (u32 *)memalign(0x1000, SOC_BUFFERSIZE);
    if (!s_socBuffer) {
        GW_LOG("memalign soc échoué -> Gateway différée (mode dégradé)");
        free(s_gw.acc); s_gw.acc = NULL;
        return -2;
    }
    rc = socInit(s_socBuffer, SOC_BUFFERSIZE);
    if (R_FAILED(rc)) {
        GW_LOG("socInit rc=%08lX -> Gateway différée (mode dégradé)", (unsigned long)rc);
        free(s_socBuffer); s_socBuffer = NULL;
        free(s_gw.acc); s_gw.acc = NULL;
        return rc;
    }
    s_threadRunning = true;
    /* Pile 64 KiB : handshake mbedtls + jansson. Priorité basse (0x3F). */
    s_thread = threadCreate(gateway_thread_main, NULL, 0x10000, 0x3F, -2, false);
    if (!s_thread) {
        GW_LOG("threadCreate gateway échoué -> Gateway différée (mode dégradé)");
        s_threadRunning = false;
        socExit();
        free(s_socBuffer); s_socBuffer = NULL;
        free(s_gw.acc); s_gw.acc = NULL;
        return -3;
    }
#else
    s_threadRunning = true;
    pthread_create(&s_thread, NULL, gateway_thread_trampoline, NULL);
#endif
    return 0;
}

void discordGatewayUpdatePresence(const presence_state_t *state) {
    gw_mutex_lock(&s_presenceLock);
    s_presence = *state;
    s_presenceValid = true;
    s_presenceDirty = true; /* envoyé par le thread réseau dès que READY */
    gw_mutex_unlock(&s_presenceLock);
}

void discordGatewayExit(void) {
    s_threadRunning = false;
    s_gw.want_close = true;
#ifdef __3DS__
    if (s_thread) { threadJoin(s_thread, U64_MAX); threadFree(s_thread); s_thread = NULL; }
    socExit();
    free(s_socBuffer); s_socBuffer = NULL;
#else
    pthread_join(s_thread, NULL);
#endif
    free(s_gw.acc); s_gw.acc = NULL; /* évite la fuite sur cycle exit/reinit (changement de compte) */
    accountBridgeExit();
}

/* ------------------------------------------------------------------------
 * Suivi d'un changement de compte actif DANS TriCord pendant que le
 * sysmodule tourne (l'utilisateur ouvre TriCord, change de compte, ou se
 * déconnecte complètement) sans jamais redémarrer la 3DS.
 *
 * TODO(emergent) — squelette à compléter, non branché à main.c pour
 * l'instant :
 *   1) Appeler cette fonction toutes les ~10-15s depuis la boucle
 *      principale de main.c (même cadence que le monitoring APT).
 *   2) Comparer le token renvoyé à s_token (comparaison en temps constant,
 *      pas strcmp, pour éviter un timing side-channel inutile même si le
 *      risque est faible en local).
 *   3) Si différent (compte changé) ou vide (déconnexion TriCord) :
 *        - fermer proprement la connexion courante (CLOSE 1000, pas
 *          juste couper le socket) ;
 *        - si vide : repasser en "attente de compte" (ne pas boucler en
 *          erreur fatale, cf. TODO dans discordGatewayInit) ;
 *        - si différent : ré-IDENTIFY avec le nouveau token (pas de RESUME
 *          possible, le session_id précédent appartient à l'ancien compte).
 *   4) Si identique : ne rien faire (évite un IDENTIFY inutile toutes les
 *      15s, cf. rate limit IDENTIFY de Discord — 1000/24h par token mais
 *      surtout pas la peine de rouvrir une session qui fonctionne déjà).
 * ---------------------------------------------------------------------- */
bool discordGatewayRefreshTokenIfChanged(void) {
    char candidate[256];
    Result rc = loadToken(candidate, sizeof(candidate));
    if (R_FAILED(rc)) {
        /* TriCord déconnecté depuis le dernier check : à gérer par l'appelant
         * (TODO(emergent) point 3 ci-dessus). Pour l'instant on ne touche
         * pas à s_token pour ne pas couper une session qui fonctionne sur
         * la seule foi d'une lecture ratée (ex. TriCord en train d'écrire
         * accounts au même instant, cf. note "pas de verrou de fichier"
         * dans tricord_account_bridge.h). */
        return false;
    }
    if (strncmp(candidate, s_token, sizeof(s_token)) == 0) {
        return false; /* pas de changement */
    }
    GW_LOG("Changement de compte détecté côté TriCord -> reconnexion requise (TODO(emergent))");
    /* TODO(emergent) : implémenter la reconnexion réelle (voir points 3-4
     * ci-dessus) ; se contenter de renvoyer true pour l'instant afin que
     * main.c puisse au moins logguer/exposer l'état via presence_state_t. */
    return true;
}

#ifdef GATEWAY_HOST_TEST
/* Test unitaire hôte du scanner streaming (tools/host_gateway_test). */
void discordGatewayTestScan(const char *json, size_t chunk, char *outSession, char *outResume) {
    gw_t g; memset(&g, 0, sizeof(g));
    scan_reset(&g);
    size_t len = strlen(json);
    for (size_t off = 0; off < len; off += chunk) {
        size_t n = len - off < chunk ? len - off : chunk;
        scan_feed(&g, (const uint8_t *)json + off, n);
    }
    strcpy(outSession, g.scan_session_id);
    strcpy(outResume, g.scan_resume_url);
}
#endif
