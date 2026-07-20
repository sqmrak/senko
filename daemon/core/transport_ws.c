#include "transport.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

#define SESS_BUF (32 * 1024)

#define WS_STATE_HANDSHAKE_SEND 0
#define WS_STATE_HANDSHAKE_RECV 1
#define WS_STATE_ESTABLISHED    2

typedef struct {
    const transport_vt_t *sub_vt;
    void                 *sub_h;
    int                  state;
    char                 path[256];
    char                 host[256];
    char                 accept[32];

    uint8_t              hs_buf[2048];
    size_t               hs_len;
    size_t               hs_off;

    uint8_t              tx_pending[SESS_BUF + 16];
    size_t               tx_pending_len;
    size_t               tx_pending_off;

    uint8_t              tx_buf[SESS_BUF + 16];

    uint8_t              rx_buf[SESS_BUF * 2 + 16];
    size_t               rx_len;
    size_t               rx_off;

    int                  rx_has_frame;
    int                  rx_opcode;
    size_t               rx_frame_left;
} ws_handle_t;

static int ws_token_ok(const char *s) {
    if (!s || !s[0]) return 0;
    for (const char *p = s; *p; ++p)
        if (*p == '\r' || *p == '\n') return 0;
    return 1;
}

static int ws_random(uint8_t *buf, size_t len) {
    if (RAND_bytes(buf, (int)len) == 1) return 0;
    return -1;
}

static int ws_b64(const uint8_t *in, size_t len, char *out, size_t cap) {
    static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t j = 0;
    if (cap < ((len + 2) / 3) * 4 + 1) return -1;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = ((uint32_t)in[i] << 16) |
                       ((i + 1 < len ? (uint32_t)in[i + 1] : 0) << 8) |
                        (i + 2 < len ? (uint32_t)in[i + 2] : 0);
        out[j++] = b64chars[(val >> 18) & 0x3F];
        out[j++] = b64chars[(val >> 12) & 0x3F];
        out[j++] = (i + 1 < len) ? b64chars[(val >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < len) ? b64chars[val & 0x3F] : '=';
    }
    out[j] = '\0';
    return 0;
}

static int gen_ws_key(char *out, size_t cap) {
    uint8_t rand_bytes[16];
    if (ws_random(rand_bytes, sizeof rand_bytes) != 0) return -1;
    return ws_b64(rand_bytes, sizeof rand_bytes, out, cap);
}

static int ws_accept_for_key(const char *key, char *out, size_t cap) {
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    char input[96];
    int n = snprintf(input, sizeof input, "%s%s", key, guid);
    if (n < 0 || (size_t)n >= sizeof input) return -1;
    if (EVP_Digest(input, (size_t)n, digest, &digest_len, EVP_sha1(), NULL) != 1)
        return -1;
    if (digest_len != 20) return -1;
    return ws_b64(digest, digest_len, out, cap);
}

static int ascii_lc(int c) {
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int header_name_eq(const char *line, size_t n, const char *name) {
    size_t nl = strlen(name);
    if (n != nl) return 0;
    for (size_t i = 0; i < n; ++i)
        if (ascii_lc((unsigned char)line[i]) != ascii_lc((unsigned char)name[i]))
            return 0;
    return 1;
}

static int ws_header_value(const char *start, const char *end,
                           const char *name, char *out, size_t cap) {
    const char *p = strstr(start, "\r\n");
    if (!p || p >= end) return -1;
    p += 2;
    while (p < end) {
        const char *le = strstr(p, "\r\n");
        if (!le || le > end) return -1;
        if (le == p) break;
        const char *colon = memchr(p, ':', (size_t)(le - p));
        if (colon && header_name_eq(p, (size_t)(colon - p), name)) {
            const char *v = colon + 1;
            while (v < le && (*v == ' ' || *v == '\t')) ++v;
            while (le > v && (le[-1] == ' ' || le[-1] == '\t')) --le;
            size_t n = (size_t)(le - v);
            if (n + 1 > cap) return -1;
            memcpy(out, v, n);
            out[n] = '\0';
            return 0;
        }
        p = le + 2;
    }
    return -1;
}

static int ws_accept_ok(ws_handle_t *h, const char *start, const char *end) {
    char got[64];
    if (ws_header_value(start, end, "Sec-WebSocket-Accept",
                        got, sizeof got) != 0)
        return 0;
    return strcmp(got, h->accept) == 0;
}

static void *ws_open_common(int fd, const transport_tls_cfg_t *cfg, const transport_vt_t *sub_vt) {
    ws_handle_t *h = (ws_handle_t *)calloc(1, sizeof *h);
    if (!h) return NULL;

    h->sub_vt = sub_vt;
    h->sub_h = sub_vt->open(fd, cfg);
    if (!h->sub_h) {
        free(h);
        return NULL;
    }

    h->state = WS_STATE_HANDSHAKE_SEND;

    snprintf(h->host, sizeof h->host, "%s",
             (cfg && cfg->ws_host && cfg->ws_host[0]) ? cfg->ws_host :
             ((cfg && cfg->sni && cfg->sni[0]) ? cfg->sni : "localhost"));
    snprintf(h->path, sizeof h->path, "%s", (cfg && cfg->path && cfg->path[0]) ? cfg->path : "/");
    if (!ws_token_ok(h->host) || !ws_token_ok(h->path)) {
        h->sub_vt->close(h->sub_h);
        free(h);
        return NULL;
    }

    char ws_key[32];
    if (gen_ws_key(ws_key, sizeof ws_key) != 0) {
        h->sub_vt->close(h->sub_h);
        free(h);
        return NULL;
    }
    if (ws_accept_for_key(ws_key, h->accept, sizeof h->accept) != 0) {
        h->sub_vt->close(h->sub_h);
        free(h);
        return NULL;
    }

    int l = snprintf((char *)h->hs_buf, sizeof h->hs_buf,
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\n"
                     "Sec-WebSocket-Version: 13\r\n"
                     "\r\n",
                     h->path, h->host, ws_key);
    if (l < 0 || (size_t)l >= sizeof h->hs_buf) {
        h->sub_vt->close(h->sub_h);
        free(h);
        return NULL;
    }
    h->hs_len = (size_t)l;
    h->hs_off = 0;

    return h;
}

static void *ws_open_tcp(int fd, const transport_tls_cfg_t *cfg) {
    return ws_open_common(fd, cfg, &transport_tcp);
}

static void *ws_open_tls(int fd, const transport_tls_cfg_t *cfg) {
    return ws_open_common(fd, cfg, &transport_tls);
}

/* weak: host unit tests that only link tcp/tls still resolve */
extern const transport_vt_t transport_reality __attribute__((weak));
static void *ws_open_reality(int fd, const transport_tls_cfg_t *cfg) {
    const transport_vt_t *vt = &transport_reality;
    if (!vt || !vt->open) return NULL;
    return ws_open_common(fd, cfg, vt);
}

static void ws_close(void *handle) {
    ws_handle_t *h = (ws_handle_t *)handle;
    if (!h) return;
    if (h->sub_h) h->sub_vt->close(h->sub_h);
    free(h);
}

static int ws_pending_append(ws_handle_t *h, const uint8_t *buf, size_t len) {
    if (len == 0) return 0;
    if (h->tx_pending_off > 0) {
        memmove(h->tx_pending, h->tx_pending + h->tx_pending_off,
                h->tx_pending_len - h->tx_pending_off);
        h->tx_pending_len -= h->tx_pending_off;
        h->tx_pending_off = 0;
    }
    if (len > sizeof h->tx_pending - h->tx_pending_len) return TRANSPORT_ERR;
    memcpy(h->tx_pending + h->tx_pending_len, buf, len);
    h->tx_pending_len += len;
    return 0;
}

static int ws_flush_pending(ws_handle_t *h) {
    while (h->tx_pending_len > h->tx_pending_off) {
        int w = h->sub_vt->write(h->sub_h, h->tx_pending + h->tx_pending_off,
                                  h->tx_pending_len - h->tx_pending_off);
        if (w > 0) {
            h->tx_pending_off += (size_t)w;
            continue;
        }
        if (w == 0) return TRANSPORT_WANT_WRITE;
        return w;
    }
    h->tx_pending_len = 0;
    h->tx_pending_off = 0;
    return 0;
}

static int ws_send_control(ws_handle_t *h, uint8_t opcode,
                           const uint8_t *payload, size_t payload_len) {
    if (payload_len > 125) return TRANSPORT_ERR;
    uint8_t frame[2 + 4 + 125];
    uint8_t mask[4];
    if (ws_random(mask, sizeof mask) != 0) return TRANSPORT_ERR;
    frame[0] = 0x80 | (opcode & 0x0f);
    frame[1] = 0x80 | (uint8_t)payload_len;
    memcpy(frame + 2, mask, sizeof mask);
    for (size_t i = 0; i < payload_len; ++i)
        frame[6 + i] = payload[i] ^ mask[i % 4];

    size_t total = 6 + payload_len;
    int pr = ws_flush_pending(h);
    if (pr < 0) {
        if (pr == TRANSPORT_WANT_WRITE)
            return ws_pending_append(h, frame, total);
        return pr;
    }

    int w = h->sub_vt->write(h->sub_h, frame, total);
    if (w < 0) {
        if (w == TRANSPORT_WANT_WRITE)
            return ws_pending_append(h, frame, total);
        return w;
    }
    if ((size_t)w < total)
        return ws_pending_append(h, frame + w, total - (size_t)w);
    return 0;
}

static int ws_handshake_step(ws_handle_t *h) {
    while (h->state == WS_STATE_HANDSHAKE_SEND) {
        int w = h->sub_vt->write(h->sub_h, h->hs_buf + h->hs_off, h->hs_len - h->hs_off);
        if (w > 0) {
            h->hs_off += (size_t)w;
            if (h->hs_off == h->hs_len) {
                h->hs_len = 0;
                h->hs_off = 0;
                h->state = WS_STATE_HANDSHAKE_RECV;
            }
            continue;
        }
        return w;
    }

    while (h->state == WS_STATE_HANDSHAKE_RECV) {
        int r = h->sub_vt->read(h->sub_h, h->hs_buf + h->hs_len, sizeof(h->hs_buf) - h->hs_len - 1);
        if (r > 0) {
            h->hs_len += (size_t)r;
            h->hs_buf[h->hs_len] = '\0';

            char *end = strstr((char *)h->hs_buf, "\r\n\r\n");
            if (end) {
                if (strncmp((char *)h->hs_buf, "HTTP/1.1 101", 12) == 0 ||
                    strncmp((char *)h->hs_buf, "HTTP/1.0 101", 12) == 0) {
                    if (!ws_accept_ok(h, (char *)h->hs_buf, end + 2))
                        return TRANSPORT_ERR;
                    h->state = WS_STATE_ESTABLISHED;

                    size_t req_len = (size_t)(end + 4 - (char *)h->hs_buf);
                    if (h->hs_len > req_len) {
                        size_t leftover = h->hs_len - req_len;
                        memcpy(h->rx_buf, h->hs_buf + req_len, leftover);
                        h->rx_len = leftover;
                    }
                    return 0;
                }
                return TRANSPORT_ERR;
            }
            if (h->hs_len >= sizeof(h->hs_buf) - 1) {
                return TRANSPORT_ERR;
            }
            continue;
        }
        return r;
    }

    return 0;
}

static int ws_write(void *handle, const uint8_t *buf, size_t len) {
    ws_handle_t *h = (ws_handle_t *)handle;
    if (!h) return TRANSPORT_ERR;

    if (h->state != WS_STATE_ESTABLISHED) {
        int hr = ws_handshake_step(h);
        if (hr < 0) return hr;
        if (h->state != WS_STATE_ESTABLISHED) return TRANSPORT_WANT_WRITE;
    }

    int pr = ws_flush_pending(h);
    if (pr < 0) return pr;

    if (len == 0) return 0;

    size_t header_size = 0;
    h->tx_buf[0] = 0x82;

    uint8_t mask[4];
    if (ws_random(mask, sizeof mask) != 0) return TRANSPORT_ERR;

    if (len < 126) {
        h->tx_buf[1] = 0x80 | (uint8_t)len;
        memcpy(h->tx_buf + 2, mask, 4);
        header_size = 6;
    } else {
        h->tx_buf[1] = 0x80 | 126;
        h->tx_buf[2] = (uint8_t)((len >> 8) & 0xFF);
        h->tx_buf[3] = (uint8_t)(len & 0xFF);
        memcpy(h->tx_buf + 4, mask, 4);
        header_size = 8;
    }

    if (header_size + len > sizeof(h->tx_buf)) {
        return TRANSPORT_ERR;
    }

    for (size_t i = 0; i < len; ++i) {
        h->tx_buf[header_size + i] = buf[i] ^ mask[i % 4];
    }

    int w = h->sub_vt->write(h->sub_h, h->tx_buf, header_size + len);
    if (w < 0) {
        if (w == TRANSPORT_WANT_WRITE) {
            memcpy(h->tx_pending, h->tx_buf, header_size + len);
            h->tx_pending_len = header_size + len;
            h->tx_pending_off = 0;
            return TRANSPORT_WANT_WRITE;
        }
        return w;
    }

    if ((size_t)w >= header_size) {
        size_t consumed = (size_t)w - header_size;
        if (consumed < len) {
            size_t rem = (header_size + len) - (size_t)w;
            memcpy(h->tx_pending, h->tx_buf + w, rem);
            h->tx_pending_len = rem;
            h->tx_pending_off = 0;
        }
        return (int)consumed;
    } else {
        size_t rem = (header_size + len) - (size_t)w;
        memcpy(h->tx_pending, h->tx_buf + w, rem);
        h->tx_pending_len = rem;
        h->tx_pending_off = 0;
        return TRANSPORT_WANT_WRITE;
    }
}

static int ws_read(void *handle, uint8_t *buf, size_t len) {
    ws_handle_t *h = (ws_handle_t *)handle;
    if (!h) return TRANSPORT_ERR;

    if (h->state != WS_STATE_ESTABLISHED) {
        int hr = ws_handshake_step(h);
        if (hr < 0) return hr;
        if (h->state != WS_STATE_ESTABLISHED) return TRANSPORT_WANT_READ;
    }

    if (len == 0) return 0;

    int pr = ws_flush_pending(h);
    if (pr < 0 && pr != TRANSPORT_WANT_WRITE) return pr;

    if (h->rx_off > 0 && h->rx_off == h->rx_len) {
        h->rx_len = 0;
        h->rx_off = 0;
    } else if (h->rx_off > sizeof(h->rx_buf) / 2) {
        memmove(h->rx_buf, h->rx_buf + h->rx_off, h->rx_len - h->rx_off);
        h->rx_len -= h->rx_off;
        h->rx_off = 0;
    }

    if (h->rx_len - h->rx_off == 0 || (h->rx_has_frame && h->rx_frame_left > 0 && h->rx_len - h->rx_off < h->rx_frame_left)) {
        int r = h->sub_vt->read(h->sub_h, h->rx_buf + h->rx_len, sizeof(h->rx_buf) - h->rx_len);
        if (r > 0) {
            h->rx_len += (size_t)r;
        } else if (r < 0) {
            if (r != TRANSPORT_WANT_READ || h->rx_len - h->rx_off == 0) {
                return r;
            }
        } else {
            return TRANSPORT_EOF;
        }
    }

    size_t unparsed = h->rx_len - h->rx_off;
    if (!h->rx_has_frame) {
        if (unparsed < 2) return TRANSPORT_WANT_READ;

        uint8_t b0 = h->rx_buf[h->rx_off];
        uint8_t b1 = h->rx_buf[h->rx_off + 1];

        int opcode = b0 & 0x0F;
        int masked = b1 & 0x80;
        size_t payload_len = b1 & 0x7F;
        size_t header_size = 2;
        int fin = b0 & 0x80;
        int rsv = b0 & 0x70;

        if (!fin || rsv || masked) return TRANSPORT_ERR;
        if (opcode != 0x02 && opcode != 0x08 && opcode != 0x09 && opcode != 0x0a)
            return TRANSPORT_ERR;

        if (payload_len == 126) header_size = 4;
        else if (payload_len == 127) header_size = 10;

        if (masked) header_size += 4;

        if (unparsed < header_size) return TRANSPORT_WANT_READ;

        size_t frame_len = 0;
        if (payload_len == 126) {
            frame_len = (h->rx_buf[h->rx_off + 2] << 8) | h->rx_buf[h->rx_off + 3];
        } else if (payload_len == 127) {
            frame_len = 0;
            if (h->rx_buf[h->rx_off + 2] & 0x80) return TRANSPORT_ERR;
            for (int i = 0; i < 8; ++i) {
                if (frame_len > (((size_t)-1) >> 8)) return TRANSPORT_ERR;
                frame_len = (frame_len << 8) | h->rx_buf[h->rx_off + 2 + i];
            }
        } else {
            frame_len = payload_len;
        }
        if ((opcode & 0x08) && frame_len > 125) return TRANSPORT_ERR;

        h->rx_off += header_size;
        h->rx_has_frame = 1;
        h->rx_opcode = opcode;
        h->rx_frame_left = frame_len;

        unparsed = h->rx_len - h->rx_off;
    }

    if (h->rx_has_frame) {
        if (h->rx_opcode == 0x08) {
            return TRANSPORT_EOF;
        }
        if (h->rx_opcode == 0x09 || h->rx_opcode == 0x0a) {
            if (unparsed < h->rx_frame_left) return TRANSPORT_WANT_READ;
            if (h->rx_opcode == 0x09) {
                int pr = ws_send_control(h, 0x0a, h->rx_buf + h->rx_off,
                                         h->rx_frame_left);
                if (pr < 0) return pr;
            }
            h->rx_off += h->rx_frame_left;
            h->rx_frame_left = 0;
            h->rx_has_frame = 0;
            return TRANSPORT_WANT_READ;
        }

        size_t take = unparsed < h->rx_frame_left ? unparsed : h->rx_frame_left;
        take = take < len ? take : len;

        if (take > 0) {
            memcpy(buf, h->rx_buf + h->rx_off, take);
            h->rx_off += take;
            h->rx_frame_left -= take;
            if (h->rx_frame_left == 0) {
                h->rx_has_frame = 0;
            }
            return (int)take;
        }
    }

    return TRANSPORT_WANT_READ;
}

/* keep pollout active while a framed websocket write is pending */
static int ws_want_write(void *handle) {
    ws_handle_t *h = (ws_handle_t *)handle;
    if (!h) return 0;
    if (h->tx_pending_len > h->tx_pending_off) return 1;
    if (h->sub_vt && h->sub_vt->want_write && h->sub_h)
        return h->sub_vt->want_write(h->sub_h);
    return 0;
}

const transport_vt_t transport_ws_tcp = {
    ws_open_tcp,
    ws_read,
    ws_write,
    NULL,
    ws_close,
    ws_want_write
};

const transport_vt_t transport_ws_reality = {
    ws_open_reality,
    ws_read,
    ws_write,
    NULL,
    ws_close,
    ws_want_write
};

const transport_vt_t transport_ws_tls = {
    ws_open_tls,
    ws_read,
    ws_write,
    NULL,
    ws_close,
    ws_want_write
};
