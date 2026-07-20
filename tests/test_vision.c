/* prove framing recovers payload and latches direct mode */
#include "../daemon/core/vision.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ok(const char *name, int cond) {
    if (cond) printf("  ok  %s\n", name);
    else { printf(" FAIL %s\n", name); fails++; }
}

int main(void) {
    uint8_t uuid[16];
    for (int i = 0; i < 16; ++i) uuid[i] = (uint8_t)(0xa0 + i);

    vision_wrap_t wrap;
    vision_unpad_t unpad;
    vision_wrap_init(&wrap, uuid);
    vision_unpad_init(&unpad, uuid);

    uint8_t payload[64];
    for (int i = 0; i < 64; ++i) payload[i] = (uint8_t)i;

    uint8_t blk[2048], app[2048];
    size_t bn = 0, an = 0;
    int dir = 0;

    ok("wrap continue",
       vision_wrap(&wrap, payload, sizeof payload, blk, sizeof blk, &bn) == 0 &&
       bn > sizeof payload);

    ok("unpad continue",
       vision_unpad(&unpad, blk, bn, app, sizeof app, &an, &dir) == 0 &&
       an == sizeof payload && memcmp(app, payload, an) == 0 &&
       dir == 0);

    /* exhaust padding before feeding tls data so direct switching is exercised */
    wrap.packets_left = 1;
    wrap.client_hello_seen = 1;
    wrap.client_tls_app_records = 1;
    /* one complete tls app data record: type 0x17, ver 0x0303, len 1, body 0 */
    uint8_t tls_app[] = { 0x17, 0x03, 0x03, 0x00, 0x01, 0x00 };
    bn = 0;
    ok("wrap direct",
       vision_wrap(&wrap, tls_app, sizeof tls_app, blk, sizeof blk, &bn) == 0 &&
       wrap.direct_sent == 1);

    dir = 0; an = 0;
    ok("unpad direct",
       vision_unpad(&unpad, blk, bn, app, sizeof app, &an, &dir) == 0 &&
       dir == 1);

    if (fails) {
        printf("%d vision checks failed\n", fails);
        return 1;
    }
    printf("all vision checks passed\n");
    return 0;
}
