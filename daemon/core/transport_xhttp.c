/* select xhttp stream mode and keep upload and download framing compatible */
#include "transport.h"
#include "reality_handshake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/rand.h>

#define XH_RX   (64 * 1024)
#define XH_TX   (32 * 1024)
#define XH_APP  (64 * 1024)
#define XH_PKT  (32 * 1024) /* max packet-up body */

enum {
    H2_DATA = 0, H2_HEADERS = 1, H2_RST_STREAM = 3, H2_SETTINGS = 4,
    H2_PING = 6, H2_GOAWAY = 7, H2_WINDOW_UPDATE = 8
};

enum { XH_MODE_ONE = 0, XH_MODE_UP = 1, XH_MODE_PKT = 2 };

enum {
    XH_ST_BOOT = 0,
    XH_ST_OPEN,
    XH_ST_FAIL
};

typedef struct {
    const transport_vt_t *sub_vt;
    void                 *sub_h;
    int                   mode;
    int                   state;
    int                   saw_settings;
    int                   peer_end;
    int                   security_tls; /* 1 if tls/reality (scheme https) */

    uint32_t peer_win, our_win;
    uint32_t up_peer_win, up_our_win; /* upload stream windows */
    uint32_t dn_peer_win, dn_our_win; /* download stream windows */
    uint32_t up_stream; /* continuous upload stream id */
    uint32_t dn_stream; /* download stream id */
    uint32_t next_sid; /* next client stream id (odd) */
    int64_t  seq;

    char host[256];
    char base_path[256]; /* normalized base, trailing / */
    char session[48];

    uint8_t tx[XH_TX];
    size_t  tx_len, tx_off;
    uint8_t rx[XH_RX];
    size_t  rx_len, rx_off;
    uint8_t app[XH_APP];
    size_t  app_len, app_off;

/* packet-up staging */
    uint8_t pkt[XH_PKT];
    size_t  pkt_len;
} xh_t;

static int h2_u24(const uint8_t *p) {
    return ((int)p[0] << 16) | ((int)p[1] << 8) | (int)p[2];
}
static void h2_put_u24(uint8_t *p, int v) {
    p[0] = (uint8_t)((v >> 16) & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)(v & 0xff);
}
static void h2_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xff);
    p[1] = (uint8_t)((v >> 16) & 0xff);
    p[2] = (uint8_t)((v >> 8) & 0xff);
    p[3] = (uint8_t)(v & 0xff);
}
static uint32_t h2_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int tx_append(xh_t *h, const uint8_t *p, size_t n) {
    if (n > sizeof h->tx - h->tx_len) return -1;
    memcpy(h->tx + h->tx_len, p, n);
    h->tx_len += n;
    return 0;
}

static int frame_append(xh_t *h, uint8_t type, uint8_t flags,
                        uint32_t stream, const uint8_t *payload, size_t plen) {
    if (plen > 0xffffff || 9 + plen > sizeof h->tx - h->tx_len) return -1;
    uint8_t hdr[9];
    h2_put_u24(hdr, (int)plen);
    hdr[3] = type; hdr[4] = flags;
    h2_put_u32(hdr + 5, stream & 0x7fffffffu);
    if (tx_append(h, hdr, 9) != 0) return -1;
    if (plen && tx_append(h, payload, plen) != 0) return -1;
    return 0;
}

static int hpack_lit(uint8_t *out, size_t cap, size_t *off,
                     const char *name, const char *val) {
    size_t nl = strlen(name), vl = strlen(val);
    if (*off + 3 + nl + vl > cap || nl > 127 || vl > 127) return -1;
    out[(*off)++] = 0x00;
    out[(*off)++] = (uint8_t)nl;
    memcpy(out + *off, name, nl); *off += nl;
    out[(*off)++] = (uint8_t)vl;
    memcpy(out + *off, val, vl); *off += vl;
    return 0;
}

static int hpack_idx_name(uint8_t *out, size_t cap, size_t *off,
                          uint8_t name_idx, const char *val) {
    size_t vl = strlen(val);
    if (*off + 2 + vl > cap || vl > 127 || name_idx >= 15) return -1;
    out[(*off)++] = name_idx; /* literal without indexing, 4-bit idx */
    out[(*off)++] = (uint8_t)vl;
    memcpy(out + *off, val, vl); *off += vl;
    return 0;
}

static void rand_pad16(char out[17]) {
    static const char b62[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    uint8_t rb[16];
    if (RAND_bytes(rb, 16) != 1) memset(rb, 0x41, 16);
    for (int i = 0; i < 16; i++) out[i] = b62[rb[i] % 62];
    out[16] = 0;
}

static void gen_session_id(char out[48]) {
/* use a uuid-shaped session id so common servers accept the request */
    uint8_t b[16];
    if (RAND_bytes(b, 16) != 1) memset(b, 0x11, 16);
    b[6] = (uint8_t)((b[6] & 0x0f) | 0x40);
    b[8] = (uint8_t)((b[8] & 0x3f) | 0x80);
    snprintf(out, 48,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

static void normalize_base_path(const char *raw, char *out, size_t cap, int trail) {
    if (!raw || !raw[0]) raw = "/";
    if (raw[0] == '/')
        snprintf(out, cap, "%s", raw);
    else
        snprintf(out, cap, "/%s", raw);
/* strip query from base for path-append modes */
    char *q = strchr(out, '?');
    if (q) *q = 0;
    size_t n = strlen(out);
    if (trail && n + 1 < cap && (n == 0 || out[n - 1] != '/')) {
        out[n] = '/'; out[n + 1] = 0;
    }
}

/* pick mode knowing which open_* was used */
static int mode_for_open(const transport_tls_cfg_t *cfg, int is_reality_or_tls) {
    const char *m = (cfg && cfg->xhttp_mode) ? cfg->xhttp_mode : "";
    if (!m[0] || strcmp(m, "auto") == 0)
        return is_reality_or_tls ? XH_MODE_ONE : XH_MODE_PKT;
    if (strcmp(m, "stream-up") == 0) return XH_MODE_UP;
    if (strcmp(m, "packet-up") == 0) return XH_MODE_PKT;
    return XH_MODE_ONE;
}

static int build_headers(uint8_t *out, size_t cap, size_t *n,
                         int is_post, const char *path, const char *host,
                         int with_grpc, size_t content_len, int has_cl) {
    size_t off = 0;
    if (is_post) out[off++] = 0x83; /* post method */
    else         out[off++] = 0x82; /* get method */
    out[off++] = 0x87; /* :scheme https */
    if (hpack_idx_name(out, cap, &off, 4, path) != 0) return -1;
    if (hpack_idx_name(out, cap, &off, 1, host) != 0) return -1;
    if (with_grpc) {
        if (hpack_lit(out, cap, &off, "content-type", "application/grpc") != 0)
            return -1;
    }
    if (has_cl) {
        char cl[16];
        snprintf(cl, sizeof cl, "%zu", content_len);
        if (hpack_lit(out, cap, &off, "content-length", cl) != 0) return -1;
    }
    *n = off;
    return 0;
}

static void path_with_padding(const char *base, char *out, size_t cap) {
    char pad[17];
    rand_pad16(pad);
    if (strchr(base, '?'))
        snprintf(out, cap, "%s&x_padding=%s", base, pad);
    else
        snprintf(out, cap, "%s?x_padding=%s", base, pad);
}

static int flush_tx(xh_t *h) {
    while (h->tx_off < h->tx_len) {
        int w = h->sub_vt->write(h->sub_h, h->tx + h->tx_off,
                                 h->tx_len - h->tx_off);
        if (w > 0) { h->tx_off += (size_t)w; continue; }
        if (w == 0) return TRANSPORT_WANT_WRITE;
        return w;
    }
    h->tx_len = h->tx_off = 0;
    return 0;
}

static int feed_window(xh_t *h, uint32_t stream, uint32_t credit) {
    uint8_t p[4];
    h2_put_u32(p, credit);
    return frame_append(h, H2_WINDOW_UPDATE, 0, stream, p, 4);
}

static int app_push(xh_t *h, const uint8_t *p, size_t n) {
    if (n == 0) return 0;
    if (h->app_off) {
        memmove(h->app, h->app + h->app_off, h->app_len - h->app_off);
        h->app_len -= h->app_off;
        h->app_off = 0;
    }
    if (h->app_len + n > sizeof h->app) return -1;
    memcpy(h->app + h->app_len, p, n);
    h->app_len += n;
    return 0;
}

static int process_frames(xh_t *h) {
    while (h->rx_len - h->rx_off >= 9) {
        const uint8_t *hdr = h->rx + h->rx_off;
        int plen = h2_u24(hdr);
        uint8_t type = hdr[3], flags = hdr[4];
        uint32_t stream = h2_u32(hdr + 5) & 0x7fffffffu;
        if (h->rx_len - h->rx_off < (size_t)(9 + plen)) break;
        const uint8_t *pay = hdr + 9;
        h->rx_off += (size_t)(9 + plen);

        if (type == H2_SETTINGS) {
            if (!(flags & 0x01)) {
/* apply the peer window before sending stream data */
                for (int i = 0; i + 6 <= plen; i += 6) {
                    uint16_t id = (uint16_t)((pay[i] << 8) | pay[i + 1]);
                    uint32_t val = h2_u32(pay + i + 2);
                    if (id == 0x4) { /* update the stream flow-control window */
                        h->peer_win = val;
                        h->up_peer_win = val;
                        h->dn_peer_win = val;
                    }
                }
                if (frame_append(h, H2_SETTINGS, 0x01, 0, NULL, 0) != 0)
                    return TRANSPORT_ERR;
            }
            h->saw_settings = 1;
            if (h->state == XH_ST_BOOT && h->saw_settings)
                h->state = XH_ST_OPEN;
            continue;
        }
        if (type == H2_WINDOW_UPDATE && plen == 4) {
            uint32_t inc = h2_u32(pay) & 0x7fffffffu;
            if (stream == 0) h->peer_win += inc;
            else if (stream == h->up_stream) h->up_peer_win += inc;
            else if (stream == h->dn_stream) h->dn_peer_win += inc;
            continue;
        }
        if (type == H2_PING && plen == 8) {
            if (!(flags & 0x01) &&
                frame_append(h, H2_PING, 0x01, 0, pay, 8) != 0)
                return TRANSPORT_ERR;
            continue;
        }
        if (type == H2_GOAWAY || type == H2_RST_STREAM) {
            h->state = XH_ST_FAIL;
            return TRANSPORT_EOF;
        }
        if (type == H2_HEADERS) {
            if (stream == h->dn_stream || stream == h->up_stream) {
/* accept; optional:status check skipped for size */
                if ((flags & 0x01) && stream == h->dn_stream) h->peer_end = 1;
            }
            continue;
        }
        if (type == H2_DATA) {
            const uint8_t *data = pay;
            size_t dlen = (size_t)plen;
            if (flags & 0x08) {
                if (dlen < 1) return TRANSPORT_ERR;
                size_t pad = data[0];
                data++; dlen--;
                if (dlen < pad) return TRANSPORT_ERR;
                dlen -= pad;
            }
            if (stream == h->dn_stream && dlen > 0) {
                if (app_push(h, data, dlen) != 0) return TRANSPORT_ERR;
                if (h->our_win > dlen) h->our_win -= (uint32_t)dlen;
                else h->our_win = 0;
                if (h->dn_our_win > dlen) h->dn_our_win -= (uint32_t)dlen;
                else h->dn_our_win = 0;
                if (h->our_win < 16000) {
                    if (feed_window(h, 0, 65535) != 0) return TRANSPORT_ERR;
                    h->our_win += 65535;
                }
                if (h->dn_our_win < 16000) {
                    if (feed_window(h, h->dn_stream, 65535) != 0)
                        return TRANSPORT_ERR;
                    h->dn_our_win += 65535;
                }
            }
            if ((flags & 0x01) && stream == h->dn_stream) h->peer_end = 1;
            continue;
        }
    }
    if (h->rx_off) {
        if (h->rx_off < h->rx_len)
            memmove(h->rx, h->rx + h->rx_off, h->rx_len - h->rx_off);
        h->rx_len -= h->rx_off;
        h->rx_off = 0;
    }
    return 0;
}

static int pump_rx(xh_t *h) {
    if (h->rx_len < sizeof h->rx) {
        int r = h->sub_vt->read(h->sub_h, h->rx + h->rx_len,
                                sizeof h->rx - h->rx_len);
        if (r > 0) h->rx_len += (size_t)r;
        else if (r == TRANSPORT_EOF) return TRANSPORT_EOF;
        else if (r < 0 && r != TRANSPORT_WANT_READ && r != TRANSPORT_WANT_WRITE)
            return r;
        else if (r < 0 && h->rx_len == 0) return r;
    }
    return process_frames(h);
}

static uint32_t alloc_sid(xh_t *h) {
    uint32_t s = h->next_sid;
    h->next_sid += 2;
    return s;
}

static int queue_boot(xh_t *h, const transport_tls_cfg_t *cfg, int is_sec) {
    h->mode = mode_for_open(cfg, is_sec);
    h->security_tls = is_sec;
    h->next_sid = 1;
    h->seq = 0;
    h->peer_win = h->our_win = 65535;
    h->up_peer_win = h->up_our_win = 65535;
    h->dn_peer_win = h->dn_our_win = 65535;

    snprintf(h->host, sizeof h->host, "%s",
             (cfg && cfg->ws_host && cfg->ws_host[0]) ? cfg->ws_host :
             ((cfg && cfg->sni && cfg->sni[0]) ? cfg->sni : "localhost"));

    int trail = (h->mode != XH_MODE_ONE);
    normalize_base_path(cfg && cfg->path ? cfg->path : "/",
                        h->base_path, sizeof h->base_path, trail);
    if (h->mode != XH_MODE_ONE)
        gen_session_id(h->session);
    else
        h->session[0] = 0;

    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    if (tx_append(h, (const uint8_t *)preface, sizeof preface - 1) != 0)
        return -1;
/* raise concurrent streams for packet-up */
    uint8_t setpay[12];
    h2_put_u32(setpay, 0x3); /* max concurrent streams */
    h2_put_u32(setpay + 4, 100);
    h2_put_u32(setpay + 8, 0); /* pad - actually need id+val pairs */
/* proper: id u16 + val u32 each */
    uint8_t settings[12];
    settings[0] = 0; settings[1] = 0x3;
    h2_put_u32(settings + 2, 128);
    settings[6] = 0; settings[7] = 0x4; /* initial window size */
    h2_put_u32(settings + 8, 1024 * 1024);
    if (frame_append(h, H2_SETTINGS, 0, 0, settings, 12) != 0) return -1;

    uint8_t block[512];
    size_t blen = 0;
    char path[400];

    if (h->mode == XH_MODE_ONE) {
        path_with_padding(h->base_path, path, sizeof path);
        if (build_headers(block, sizeof block, &blen, 1, path, h->host, 1, 0, 0) != 0)
            return -1;
        h->up_stream = h->dn_stream = alloc_sid(h); /* stream 1 both ways */
        if (frame_append(h, H2_HEADERS, 0x04, h->up_stream, block, blen) != 0)
            return -1;
    } else if (h->mode == XH_MODE_UP) {
/* open the long-lived download stream first */
        snprintf(path, sizeof path, "%s%s/", h->base_path, h->session);
        char path_pad[420];
        path_with_padding(path, path_pad, sizeof path_pad);
        if (build_headers(block, sizeof block, &blen, 0, path_pad, h->host, 0, 0, 0) != 0)
            return -1;
        h->dn_stream = alloc_sid(h);
        if (frame_append(h, H2_HEADERS, 0x04 | 0x01, h->dn_stream, block, blen) != 0)
            return -1; /* end the get request with headers */

/* keep upload on its own stream for stream-up mode */
        blen = 0;
        snprintf(path, sizeof path, "%s%s/", h->base_path, h->session);
        path_with_padding(path, path_pad, sizeof path_pad);
        if (build_headers(block, sizeof block, &blen, 1, path_pad, h->host, 1, 0, 0) != 0)
            return -1;
        h->up_stream = alloc_sid(h);
        if (frame_append(h, H2_HEADERS, 0x04, h->up_stream, block, blen) != 0)
            return -1;
    } else { /* packet-up opens download first and posts per write */
        snprintf(path, sizeof path, "%s%s/", h->base_path, h->session);
        char path_pad[420];
        path_with_padding(path, path_pad, sizeof path_pad);
        if (build_headers(block, sizeof block, &blen, 0, path_pad, h->host, 0, 0, 0) != 0)
            return -1;
        h->dn_stream = alloc_sid(h);
        h->up_stream = 0;
        if (frame_append(h, H2_HEADERS, 0x04 | 0x01, h->dn_stream, block, blen) != 0)
            return -1;
    }

    h->state = XH_ST_BOOT;
    return 0;
}

static int send_packet(xh_t *h, const uint8_t *buf, size_t len) {
    if (len == 0 || len > XH_PKT) return -1;
    char path[420], path_pad[440];
    snprintf(path, sizeof path, "%s%s/%lld",
             h->base_path, h->session, (long long)h->seq);
    h->seq++;
    path_with_padding(path, path_pad, sizeof path_pad);

    uint8_t block[512];
    size_t blen = 0;
    if (build_headers(block, sizeof block, &blen, 1, path_pad, h->host, 0, len, 1) != 0)
        return -1;
    uint32_t sid = alloc_sid(h);
/* leave end stream for the data frame */
    if (frame_append(h, H2_HEADERS, 0x04, sid, block, blen) != 0) return -1;
    if (frame_append(h, H2_DATA, 0x01, sid, buf, len) != 0) return -1; /* end stream */
    return 0;
}

static void *xh_open_common(int fd, const transport_tls_cfg_t *cfg,
                            const transport_vt_t *sub, int is_sec) {
    xh_t *h = (xh_t *)calloc(1, sizeof *h);
    if (!h) return NULL;
    h->sub_vt = sub;
    h->sub_h = sub->open(fd, cfg);
    if (!h->sub_h) { free(h); return NULL; }
    if (queue_boot(h, cfg, is_sec) != 0) {
        sub->close(h->sub_h);
        free(h);
        return NULL;
    }
    return h;
}

static void *xh_open_tcp(int fd, const transport_tls_cfg_t *cfg) {
    return xh_open_common(fd, cfg, &transport_tcp, 0);
}
static void *xh_open_tls(int fd, const transport_tls_cfg_t *cfg) {
    return xh_open_common(fd, cfg, &transport_tls, 1);
}
static void *xh_open_reality(int fd, const transport_tls_cfg_t *cfg) {
    return xh_open_common(fd, cfg, &transport_reality, 1);
}

static void xh_close(void *handle) {
    xh_t *h = (xh_t *)handle;
    if (!h) return;
    if (h->sub_h) h->sub_vt->close(h->sub_h);
    free(h);
}

static int xh_write(void *handle, const uint8_t *buf, size_t len) {
    xh_t *h = (xh_t *)handle;
    if (!h || h->state == XH_ST_FAIL) return TRANSPORT_ERR;

    int fr = flush_tx(h);
    if (fr < 0) return fr;

    if (h->state != XH_ST_OPEN) {
        int pr = pump_rx(h);
        if (pr < 0 && pr != TRANSPORT_WANT_READ && pr != TRANSPORT_WANT_WRITE)
            return pr;
        fr = flush_tx(h);
        if (fr < 0) return fr;
        if (h->state != XH_ST_OPEN) return TRANSPORT_WANT_WRITE;
    }
    if (len == 0) return 0;

    if (h->mode == XH_MODE_PKT) {
/* batch writes so packet-up does not flood streams */
        size_t room = sizeof h->pkt - h->pkt_len;
        size_t take = len < room ? len : room;
        if (take == 0) {
            if (send_packet(h, h->pkt, h->pkt_len) != 0)
                return TRANSPORT_WANT_WRITE;
            h->pkt_len = 0;
            take = len < sizeof h->pkt ? len : sizeof h->pkt;
        }
        memcpy(h->pkt + h->pkt_len, buf, take);
        h->pkt_len += take;
/* send when reasonably full or always send immediately for low latency */
        if (h->pkt_len >= 1024 || take == len) {
            if (send_packet(h, h->pkt, h->pkt_len) != 0)
                return TRANSPORT_WANT_WRITE;
            h->pkt_len = 0;
        }
        fr = flush_tx(h);
        if (fr < 0 && fr != TRANSPORT_WANT_WRITE) return fr;
        return (int)take;
    }

/* stream modes write data on the upload stream */
    size_t max = len;
    if (h->peer_win < max) max = h->peer_win;
    if (h->up_peer_win < max) max = h->up_peer_win;
    if (max == 0) {
        int pr = pump_rx(h);
        if (pr < 0 && pr != TRANSPORT_WANT_READ) return pr;
        fr = flush_tx(h);
        if (fr < 0) return fr;
        return TRANSPORT_WANT_WRITE;
    }
    if (max > 16384) max = 16384;
    if (frame_append(h, H2_DATA, 0, h->up_stream, buf, max) != 0)
        return TRANSPORT_WANT_WRITE;
    h->peer_win -= (uint32_t)max;
    h->up_peer_win -= (uint32_t)max;
    fr = flush_tx(h);
    if (fr < 0 && fr != TRANSPORT_WANT_WRITE) return fr;
    return (int)max;
}

static int xh_read(void *handle, uint8_t *buf, size_t len) {
    xh_t *h = (xh_t *)handle;
    if (!h || h->state == XH_ST_FAIL) return TRANSPORT_ERR;
    if (len == 0) return 0;

    int fr = flush_tx(h);
    if (fr < 0 && fr != TRANSPORT_WANT_WRITE) return fr;

/* flush pending packet-up if idle? leave for write path */

    if (h->app_off < h->app_len) {
        size_t n = h->app_len - h->app_off;
        if (n > len) n = len;
        memcpy(buf, h->app + h->app_off, n);
        h->app_off += n;
        if (h->app_off == h->app_len) h->app_off = h->app_len = 0;
        return (int)n;
    }

    int pr = pump_rx(h);
    if (pr < 0 && pr != TRANSPORT_WANT_READ && pr != TRANSPORT_WANT_WRITE)
        return pr;
    fr = flush_tx(h);
    if (fr < 0 && fr != TRANSPORT_WANT_WRITE) return fr;

    if (h->app_off < h->app_len) {
        size_t n = h->app_len - h->app_off;
        if (n > len) n = len;
        memcpy(buf, h->app + h->app_off, n);
        h->app_off += n;
        if (h->app_off == h->app_len) h->app_off = h->app_len = 0;
        return (int)n;
    }
    if (h->peer_end) return TRANSPORT_EOF;
    return TRANSPORT_WANT_READ;
}

static int xh_want_write(void *handle) {
    xh_t *h = (xh_t *)handle;
    if (!h) return 0;
    if (h->tx_len > h->tx_off) return 1;
    if (h->sub_vt && h->sub_vt->want_write && h->sub_h)
        return h->sub_vt->want_write(h->sub_h);
    return 0;
}

const transport_vt_t transport_xhttp_tcp = {
    xh_open_tcp, xh_read, xh_write, NULL, xh_close, xh_want_write
};
const transport_vt_t transport_xhttp_tls = {
    xh_open_tls, xh_read, xh_write, NULL, xh_close, xh_want_write
};
const transport_vt_t transport_xhttp_reality = {
    xh_open_reality, xh_read, xh_write, NULL, xh_close, xh_want_write
};
