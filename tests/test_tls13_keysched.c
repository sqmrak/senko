/* traffic-secret chain: early -> handshake -> master must be deterministic*/
#include "../daemon/core/tls13_keysched.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ok(const char *name, int cond) {
    if (cond) printf("  ok  %s\n", name);
    else { printf(" FAIL %s\n", name); fails++; }
}

int main(void) {
    uint8_t early[32], hs[32], master[32];
    uint8_t ecdhe[32], empty_hash[32];
    uint8_t key[16], iv[12];
    uint8_t again[32];

    memset(ecdhe, 0x5a, sizeof ecdhe);
    memset(empty_hash, 0, sizeof empty_hash);
    /* empty transcript hash is sha256("") for the derived step; keysched
       hashes empty itself for the no-psk early secret path*/

    ok("early secret", tls13_early_secret(NULL, 0, early) == TLS13_OK);
    ok("early stable",
       tls13_early_secret(NULL, 0, again) == TLS13_OK &&
       memcmp(early, again, 32) == 0);

    ok("handshake secret",
       tls13_handshake_secret(early, ecdhe, sizeof ecdhe, hs) == TLS13_OK);
    ok("master secret", tls13_master_secret(hs, master) == TLS13_OK);

    ok("c hs traffic",
       tls13_traffic_secret(hs, "c hs traffic", empty_hash, again) == TLS13_OK);
    ok("traffic keys",
       tls13_traffic_keys(again, key, iv) == TLS13_OK);

    /* different ecdhe must change the handshake secret */
    ecdhe[0] ^= 0xff;
    uint8_t hs2[32];
    ok("handshake differs",
       tls13_handshake_secret(early, ecdhe, sizeof ecdhe, hs2) == TLS13_OK &&
       memcmp(hs, hs2, 32) != 0);

    if (fails) {
        printf("%d tls13_keysched checks failed\n", fails);
        return 1;
    }
    printf("all tls13_keysched checks passed\n");
    return 0;
}
