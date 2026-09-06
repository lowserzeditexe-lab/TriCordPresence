#pragma once
#include <stdbool.h>
#ifdef __3DS__
#include <3ds/types.h>
#else
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
#endif

// Ce header est partagé (copié) entre le sysmodule et le plugin .3gx :
// c'est le format du message envoyé via le port IPC "presence:d".

typedef enum {
    PRESENCE_KIND_UNKNOWN = 0,
    PRESENCE_KIND_IDLE,     // menu HOME
    PRESENCE_KIND_IN_GAME,  // un titre "Application" est actif
} presence_kind_t;

typedef struct {
    presence_kind_t kind;
    u64 title_id;
    char game_name[64]; // nom résolu localement (base title id -> nom),
                         // tronqué si besoin ; toujours NUL-terminé
} presence_state_t;

static inline bool presenceStateEquals(const presence_state_t *a, const presence_state_t *b) {
    return a->kind == b->kind && a->title_id == b->title_id;
}

// --- Protocole IPC sur le port global "presence:d" -----------------------
// Une seule commande. Tout passe dans le command buffer TLS (pas de
// descripteurs de traduction), ce qui évite toute contrainte de permission
// sur des buffers mappés entre le jeu et le sysmodule.
//
//   Requête  : [0] IPC_MakeHeader(PRESENCE_CMD_GET_STATE, 0, 0)
//   Réponse  : [0] IPC_MakeHeader(PRESENCE_CMD_GET_STATE, 1 + PRESENCE_IPC_STATE_WORDS, 0)
//              [1] Result
//              [2] kind (u32)
//              [3] title_id bits 0-31   [4] title_id bits 32-63
//              [5..20] game_name (64 octets, NUL-terminé)
#define PRESENCE_IPC_PORT_NAME     "presence:d"
#define PRESENCE_CMD_GET_STATE     0x0001
#define PRESENCE_IPC_STATE_WORDS   (1 + 2 + 16)

static inline void presenceStatePack(const presence_state_t *st, u32 *words) {
    words[0] = (u32)st->kind;
    words[1] = (u32)(st->title_id & 0xFFFFFFFFULL);
    words[2] = (u32)(st->title_id >> 32);
    char name[64];
    for (int i = 0; i < 64; i++) name[i] = st->game_name[i];
    name[63] = '\0';
    const u32 *src = (const u32 *)name;
    for (int i = 0; i < 16; i++) words[3 + i] = src[i];
}

static inline void presenceStateUnpack(presence_state_t *st, const u32 *words) {
    st->kind = (presence_kind_t)words[0];
    st->title_id = ((u64)words[2] << 32) | words[1];
    u32 *dst = (u32 *)st->game_name;
    for (int i = 0; i < 16; i++) dst[i] = words[3 + i];
    st->game_name[63] = '\0';
}
