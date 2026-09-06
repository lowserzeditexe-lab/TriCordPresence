# Build hôte du client Gateway (Linux : apt install libmbedtls-dev libwslay-dev libjansson-dev)
CC      ?= cc
CFLAGS  := -O1 -g -Wall -DGATEWAY_HOST_TEST -I../../sysmodule/source
LDLIBS  := -lwslay -lmbedtls -lmbedx509 -lmbedcrypto -ljansson -lpthread

gateway_test: main.c ../../sysmodule/source/discord_gateway.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm -f gateway_test
