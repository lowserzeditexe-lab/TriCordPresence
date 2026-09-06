// Test hôte (Linux) de sysmodule/source/discord_gateway.c contre la vraie
// Gateway Discord : valide TLS + upgrade WebSocket + HELLO + IDENTIFY sans
// console. Avec un token bidon, Discord ferme avec le code 4004 — c'est le
// résultat attendu (le protocole est correct jusqu'à l'authentification).
//
//   make -C tools/host_gateway_test && TRICORD_CONFIG=/tmp/cfg ./tools/host_gateway_test/gateway_test
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../../sysmodule/source/presence_state.h"
#include "../../sysmodule/source/discord_gateway.h"

void discordGatewayTestScan(const char *json, size_t chunk, char *outSession, char *outResume);

static int test_scanner(void) {
    char sid[128], url[128];
    const char *ready = "{\"t\":\"READY\",\"s\":1,\"op\":0,\"d\":{\"v\":10,\"user\":{\"id\":\"1\",\"username\":\"x\"},"
        "\"sessions\":[{\"session_id\":\"WRONG_NESTED\",\"status\":\"online\"},{\"session_id\":\"all\"}],"
        "\"guilds\":[{\"name\":\"fake \\\"session_id\\\":\\\"x\\\" nope\",\"o\":{\"session_id\":\"deep\"}}],\"resume_gateway_url\":\"wss://gateway-us-east1-b.discord.gg\","
        "\"session_id\":\"3f2a9c0e1b7d4e6f8a9b0c1d2e3f4a5b\",\"private_channels\":[]}}";
    int fails = 0;
    for (size_t chunk = 1; chunk <= 64; chunk *= 2) {
        discordGatewayTestScan(ready, chunk, sid, url);
        if (strcmp(sid, "3f2a9c0e1b7d4e6f8a9b0c1d2e3f4a5b") || strcmp(url, "wss://gateway-us-east1-b.discord.gg")) {
            printf("scanner KO (chunk=%zu): sid='%s' url='%s'\n", chunk, sid, url); fails++;
        }
    }
    printf(fails ? "scanner streaming: ECHEC\n" : "scanner streaming: OK (chunks 1..64)\n");
    return fails;
}

int main(int argc, char **argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 20;
    if (test_scanner()) return 2;
    if (R_FAILED(discordGatewayInit())) { printf("config/token introuvable (TRICORD_CONFIG)\n"); return 1; }

    presence_state_t st = { PRESENCE_KIND_IN_GAME, 0x0004000000055D00ULL, "Mario Kart 7" };
    discordGatewayUpdatePresence(&st);

    sleep(seconds);
    discordGatewayExit();
    return 0;
}
