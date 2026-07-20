#define _DEFAULT_SOURCE

#include "session.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static void ok(const char *what, int cond) {
    if (cond) return;
    g_fail++;
    fprintf(stderr, "FAIL %s\n", what);
}

typedef struct {
    const uint8_t *rx;
    size_t rx_len;
    int rx_done;
    int write_calls;
    int raw_calls;
    int mark_calls;
    uint8_t raw_buf[64];
    size_t raw_len;
} fake_transport_t;

static void *fake_open(int fd, const transport_tls_cfg_t *cfg) {
    (void)fd;
    (void)cfg;
    return NULL;
}

static int fake_read(void *h, uint8_t *buf, size_t len) {
    fake_transport_t *ft = (fake_transport_t *)h;
    if (ft->rx_done) return TRANSPORT_WANT_READ;
    size_t n = ft->rx_len < len ? ft->rx_len : len;
    memcpy(buf, ft->rx, n);
    ft->rx_done = 1;
    return (int)n;
}

static int fake_write(void *h, const uint8_t *buf, size_t len) {
    (void)buf;
    fake_transport_t *ft = (fake_transport_t *)h;
    /* len 0 is a flush probe (reality wpend); not a real app write */
    if (len == 0) return 0;
    ft->write_calls++;
    return (int)len;
}

static int fake_raw_write(void *h, const uint8_t *buf, size_t len) {
    fake_transport_t *ft = (fake_transport_t *)h;
    if (len == 0) {
        ft->mark_calls++;
        return 0;
    }
    ft->raw_calls++;
    if (len > sizeof ft->raw_buf) len = sizeof ft->raw_buf;
    memcpy(ft->raw_buf, buf, len);
    ft->raw_len = len;
    return (int)len;
}

static void fake_close(void *h) {
    (void)h;
}

static const transport_vt_t fake_vt = {
    fake_open, fake_read, fake_write, fake_raw_write, fake_close, NULL
};

int main(void) {
    uint8_t uuid[VLESS_UUID_LEN];
    for (size_t i = 0; i < sizeof uuid; ++i) uuid[i] = (uint8_t)(i + 1);

    uint8_t direct[16 + 5 + 3];
    memcpy(direct, uuid, 16);
    direct[16] = VISION_CMD_DIRECT;
    direct[17] = 0;
    direct[18] = 0;
    direct[19] = 0;
    direct[20] = 0;
    memcpy(direct + 21, "RAW", 3);

    fake_transport_t ft;
    memset(&ft, 0, sizeof ft);
    ft.rx = direct;
    ft.rx_len = sizeof direct;

    session_t s;
    ok("session init", session_init(&s, &fake_vt, &ft, VL_PROTO_VLESS,
                                    uuid, "xtls-rprx-vision", NULL, NULL) == SESS_OK);
    s.state = SESS_RELAY;
    s.u.vc.state = VC_ST_OPEN;

    ok("pump direct", session_pump_remote(&s) == SESS_OK);
    ok("downstream direct latched", s.vision_downstream_direct == 1);
    ok("upstream direct latched", s.vision_upstream_direct == 1);
    ok("transport raw marked", ft.mark_calls == 1);

    uint8_t out[8];
    size_t got = session_take_client(&s, out, sizeof out);
    ok("direct payload delivered", got == 3 && memcmp(out, "RAW", 3) == 0);

    size_t consumed = 0;
    ok("feed raw client", session_feed_client(&s, (const uint8_t *)"GET", 3, &consumed) == SESS_OK);
    ok("client consumed", consumed == 3);
    ok("raw write used", ft.raw_calls == 1);
    ok("normal write not used", ft.write_calls == 0);
    ok("raw payload", ft.raw_len == 3 && memcmp(ft.raw_buf, "GET", 3) == 0);

    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("all session direct checks passed\n");
    return 0;
}
