/* prove tls detection and framing transitions stay aligned */
#include "../daemon/core/vision.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ok(const char *name, int cond) {
    if (cond) printf("  ok  %s\n", name);
    else { printf(" FAIL %s\n", name); fails++; }
}

static size_t server_hello(uint8_t *out, uint16_t cipher, int tls13) {
    size_t ext_len = tls13 ? 36 : 0;
    size_t body_len = 2 + 32 + 1 + 2 + 1 + 2 + 6 + ext_len;
    size_t record_len = 4 + body_len;
    memset(out, 0, 128);
    out[0] = 0x16; out[1] = 0x03; out[2] = 0x03;
    out[3] = (uint8_t)(record_len >> 8); out[4] = (uint8_t)record_len;
    out[5] = 0x02;
    out[6] = (uint8_t)((body_len - 4) >> 16);
    out[7] = (uint8_t)((body_len - 4) >> 8);
    out[8] = (uint8_t)(body_len - 4);
    out[9] = 0x03; out[10] = 0x03;
    out[43] = 0;
    out[44] = (uint8_t)(cipher >> 8); out[45] = (uint8_t)cipher;
    out[46] = 0;
    out[47] = (uint8_t)(ext_len >> 8); out[48] = (uint8_t)ext_len;
    if (tls13) {
        out[49] = 0x00; out[50] = 0x2b;
        out[51] = 0x00; out[52] = 0x02;
        out[53] = 0x03; out[54] = 0x04;
        out[55] = 0xff; out[56] = 0x00;
        out[57] = 0x00; out[58] = 0x1a;
    }
    return 5 + record_len;
}

int main(void) {
    uint8_t uuid[16];
    for (int i = 0; i < 16; ++i) uuid[i] = (uint8_t)(0xa0 + i);

    vision_traffic_t traffic;
    vision_wrap_t wrap;
    vision_unpad_t unpad;
    vision_traffic_init(&traffic);
    vision_wrap_init(&wrap, uuid);
    vision_unpad_init(&unpad, uuid);
    vision_wrap_bind_traffic(&wrap, &traffic);
    vision_unpad_bind_traffic(&unpad, &traffic);

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
       an == sizeof payload && memcmp(app, payload, an) == 0 && dir == 0);

    uint8_t client_hello[] = { 0x16, 0x03, 0x03, 0x00, 0x04,
                               0x01, 0x00, 0x00, 0x00 };
    uint8_t hello[128];
    size_t hello_len = server_hello(hello, 0x1301, 1);
    vision_traffic_init(&traffic);
    vision_wrap_init(&wrap, uuid);
    vision_wrap_bind_traffic(&wrap, &traffic);
    ok("client hello detected",
       vision_wrap(&wrap, client_hello, sizeof client_hello,
                   blk, sizeof blk, &bn) == 0 && traffic.is_tls == 1);
    vision_filter_tls(&traffic, hello, hello_len);
    ok("tls 1.3 enables direct",
       traffic.enable_xtls == 1 && traffic.is_tls12_or_above == 1);
    uint8_t app_record[] = { 0x17, 0x03, 0x03, 0x00, 0x01, 0x00 };
    ok("tls 1.3 direct frame",
       vision_wrap(&wrap, app_record, sizeof app_record,
                   blk, sizeof blk, &bn) == 0 && wrap.direct_sent == 1);

    vision_traffic_init(&traffic);
    vision_wrap_init(&wrap, uuid);
    vision_wrap_bind_traffic(&wrap, &traffic);
    hello_len = server_hello(hello, 0x009c, 0);
    vision_filter_tls(&traffic, hello, hello_len);
    ok("tls 1.2 keeps direct disabled",
       traffic.is_tls12_or_above == 1 && traffic.enable_xtls == 0);
    ok("tls 1.2 end frame",
       vision_wrap(&wrap, app_record, sizeof app_record,
                   blk, sizeof blk, &bn) == 0 && wrap.end_sent == 1 &&
       wrap.direct_sent == 0);

    vision_traffic_init(&traffic);
    hello_len = server_hello(hello, 0x1301, 1);
    hello_len = hello_len > 60 ? 55 : hello_len;
    vision_filter_tls(&traffic, hello, hello_len);
    ok("short server hello stays conservative", traffic.enable_xtls == 0);

    uint8_t end_frame[64];
    memcpy(end_frame, uuid, 16);
    end_frame[16] = VISION_CMD_END;
    end_frame[17] = 0; end_frame[18] = 3;
    end_frame[19] = 0; end_frame[20] = 0;
    memcpy(end_frame + 21, "abc", 3);
    memcpy(end_frame + 24, "tail", 4);
    vision_unpad_init(&unpad, uuid);
    an = 0; dir = 0;
    ok("end frame preserves tail",
       vision_unpad(&unpad, end_frame, 28, app, sizeof app, &an, &dir) == 0 &&
       an == 7 && memcmp(app, "abctail", 7) == 0 &&
       unpad.framing_done == 1 && dir == 0);

    if (fails) {
        printf("%d vision checks failed\n", fails);
        return 1;
    }
    printf("all vision checks passed\n");
    return 0;
}
