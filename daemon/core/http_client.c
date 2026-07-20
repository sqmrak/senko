#include "http_client.h"
#include <stdio.h>
#include <string.h>

void hc_init(http_client_t *c, const char *user, const char *pass, const vless_dest_t *dest) {
    if (!c || !dest) return;
    memset(c, 0, sizeof *c);
    if (user) {
        size_t ul = strlen(user);
        if (ul >= sizeof c->user) ul = sizeof c->user - 1;
        memcpy(c->user, user, ul);
        c->user[ul] = '\0';
    }
    if (pass) {
        size_t pl = strlen(pass);
        if (pl >= sizeof c->pass) pl = sizeof c->pass - 1;
        memcpy(c->pass, pass, pl);
        c->pass[pl] = '\0';
    }
    c->dest = *dest;
    c->state = HC_ST_INIT;
}

static void b64_encode(const char *in, size_t in_len, char *out) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, j = 0;
    for (; i < in_len; i += 3) {
        uint32_t val = (uint32_t)(unsigned char)in[i] << 16;
        int have = 1;
        if (i + 1 < in_len) {
            val |= (uint32_t)(unsigned char)in[i + 1] << 8;
            have = 2;
        }
        if (i + 2 < in_len) {
            val |= (uint32_t)(unsigned char)in[i + 2];
            have = 3;
        }
        out[j++] = table[(val >> 18) & 0x3f];
        out[j++] = table[(val >> 12) & 0x3f];
        out[j++] = (have >= 2) ? table[(val >> 6) & 0x3f] : '=';
        out[j++] = (have >= 3) ? table[val & 0x3f] : '=';
    }
    out[j] = '\0';
}

int hc_make_request(http_client_t *c, uint8_t *out, size_t cap, size_t *out_len) {
    if (!c || !out || !out_len) return HC_ERR_ARG;
    if (c->state != HC_ST_INIT) return HC_ERR_PROTO;

    char target[512];
    if (c->dest.atyp == VLESS_ADDR_IPV4) {
        snprintf(target, sizeof target, "%d.%d.%d.%d:%d",
                 c->dest.host_addr[0], c->dest.host_addr[1],
                 c->dest.host_addr[2], c->dest.host_addr[3],
                 c->dest.port);
    } else if (c->dest.atyp == VLESS_ADDR_DOMAIN) {
        snprintf(target, sizeof target, "%s:%d", c->dest.domain, c->dest.port);
    } else if (c->dest.atyp == VLESS_ADDR_IPV6) {
        snprintf(target, sizeof target, "[%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x]:%d",
                 c->dest.host_addr[0], c->dest.host_addr[1], c->dest.host_addr[2], c->dest.host_addr[3],
                 c->dest.host_addr[4], c->dest.host_addr[5], c->dest.host_addr[6], c->dest.host_addr[7],
                 c->dest.host_addr[8], c->dest.host_addr[9], c->dest.host_addr[10], c->dest.host_addr[11],
                 c->dest.host_addr[12], c->dest.host_addr[13], c->dest.host_addr[14], c->dest.host_addr[15],
                 c->dest.port);
    } else {
        return HC_ERR_PROTO;
    }

    char req[2048];
    int n = snprintf(req, sizeof req, "CONNECT %s HTTP/1.1\r\nHost: %s\r\n", target, target);
    if (n < 0 || (size_t)n >= sizeof req) return HC_ERR_PROTO;

    if (c->user[0]) {
        char creds[256], b64[512];
        snprintf(creds, sizeof creds, "%s:%s", c->user, c->pass);
        b64_encode(creds, strlen(creds), b64);
        int m = snprintf(req + n, sizeof req - n, "Proxy-Authorization: Basic %s\r\n", b64);
        if (m < 0) return HC_ERR_PROTO;
        n += m;
    }

    int m = snprintf(req + n, sizeof req - n, "\r\n");
    if (m < 0 || (size_t)(n + m) >= sizeof req) return HC_ERR_PROTO;
    n += m;

    if ((size_t)n > cap) return HC_ERR_ARG;
    memcpy(out, req, (size_t)n);
    *out_len = (size_t)n;
    c->state = HC_ST_REQ_SENT;
    return HC_OK;
}

int hc_feed(http_client_t *c, const uint8_t *in, size_t in_len, size_t *consumed) {
    if (!c || !in || !consumed) return HC_ERR_ARG;
    *consumed = 0;

    if (c->state != HC_ST_REQ_SENT) return HC_ERR_PROTO;

    size_t room = sizeof c->rbuf - c->rbuf_len;
    size_t take = in_len < room ? in_len : room;
    if (take > 0) {
        memcpy(c->rbuf + c->rbuf_len, in, take);
        c->rbuf_len += take;
    }

    const uint8_t *limit = c->rbuf + c->rbuf_len;
    const uint8_t *hdr_end = NULL;
    for (const uint8_t *p = c->rbuf; p + 3 < limit; ++p) {
        if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
            hdr_end = p + 4;
            break;
        }
    }

    if (!hdr_end) {
        if (room == 0) {
            c->state = HC_ST_ERROR;
            return HC_ERR_PROTO;
        }
        return HC_NEED_MORE;
    }

    if (c->rbuf_len < 12) { c->state = HC_ST_ERROR; return HC_ERR_PROTO; }
    if (strncmp((const char *)c->rbuf, "HTTP/", 5) != 0) { c->state = HC_ST_ERROR; return HC_ERR_PROTO; }

    const char *status = strchr((const char *)c->rbuf, ' ');
    if (!status || status >= (const char *)hdr_end) { c->state = HC_ST_ERROR; return HC_ERR_PROTO; }
    status++;

    int code = 0;
    if (sscanf(status, "%d", &code) != 1) { c->state = HC_ST_ERROR; return HC_ERR_PROTO; }

    size_t consumed_from_in = hdr_end - c->rbuf;
    size_t prev_len = c->rbuf_len - take;
    if (consumed_from_in > prev_len) {
        *consumed = consumed_from_in - prev_len;
    } else {
        *consumed = 0;
    }

    if (code == 200) {
        c->state = HC_ST_OPEN;
        return HC_OK;
    } else if (code == 407) {
        c->state = HC_ST_ERROR;
        return HC_ERR_AUTH;
    } else {
        c->state = HC_ST_ERROR;
        return HC_ERR_PROTO;
    }
}
