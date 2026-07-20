#include "socks5_client.h"
#include <string.h>

void s5c_init(socks5_client_t *c, const char *user, const char *pass, const vless_dest_t *dest) {
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
    c->state = S5C_ST_INIT;
}

int s5c_make_request(socks5_client_t *c, uint8_t *out, size_t cap, size_t *out_len) {
    if (!c || !out || !out_len) return S5C_ERR_ARG;
    if (c->state != S5C_ST_INIT) return S5C_ERR_PROTO;

    size_t len = 0;
    out[len++] = 0x05;
    if (c->user[0]) {
        out[len++] = 0x02; /* 2 methods */
        out[len++] = 0x00; /* no auth */
        out[len++] = 0x02; /* username/password */
    } else {
        out[len++] = 0x01; /* 1 method */
        out[len++] = 0x00; /* no auth */
    }

    if (len > cap) return S5C_ERR_ARG;
    *out_len = len;
    c->state = S5C_ST_GREETING_SENT;
    return S5C_OK;
}

static int format_connect(socks5_client_t *c, uint8_t *out, size_t cap, size_t *out_len) {
    size_t len = 0;
    out[len++] = 0x05; /* version 5 */
    out[len++] = 0x01; /* cmd connect */
    out[len++] = 0x00; /* rsv */
    out[len++] = (uint8_t)c->dest.atyp;

    if (c->dest.atyp == VLESS_ADDR_IPV4) {
        memcpy(out + len, c->dest.host_addr, 4);
        len += 4;
    } else if (c->dest.atyp == VLESS_ADDR_DOMAIN) {
        size_t dl = strlen(c->dest.domain);
        if (dl > 255) return S5C_ERR_PROTO;
        out[len++] = (uint8_t)dl;
        memcpy(out + len, c->dest.domain, dl);
        len += dl;
    } else if (c->dest.atyp == VLESS_ADDR_IPV6) {
        memcpy(out + len, c->dest.host_addr, 16);
        len += 16;
    } else {
        return S5C_ERR_PROTO;
    }

    out[len++] = (uint8_t)(c->dest.port >> 8);
    out[len++] = (uint8_t)(c->dest.port & 0xff);

    if (len > cap) return S5C_ERR_ARG;
    *out_len = len;
    c->state = S5C_ST_REQ_SENT;
    return S5C_OK;
}

int s5c_feed(socks5_client_t *c, const uint8_t *in, size_t in_len, size_t *consumed, uint8_t *out, size_t cap, size_t *out_len) {
    if (!c || !in || !consumed || !out || !out_len) return S5C_ERR_ARG;
    *consumed = 0;
    *out_len = 0;

    if (c->state == S5C_ST_GREETING_SENT) {
        if (in_len < 2) return S5C_NEED_MORE;
        if (in[0] != 0x05) { c->state = S5C_ST_ERROR; return S5C_ERR_PROTO; }

        uint8_t method = in[1];
        *consumed = 2;

        if (method == 0x00) {
            return format_connect(c, out, cap, out_len);
        } else if (method == 0x02) {
            if (!c->user[0]) { c->state = S5C_ST_ERROR; return S5C_ERR_AUTH; }
            size_t ulen = strlen(c->user);
            size_t plen = strlen(c->pass);
            if (ulen > 255 || plen > 255) { c->state = S5C_ST_ERROR; return S5C_ERR_PROTO; }

            size_t len = 0;
            out[len++] = 0x01; /* subnegotiation version */
            out[len++] = (uint8_t)ulen;
            memcpy(out + len, c->user, ulen);
            len += ulen;
            out[len++] = (uint8_t)plen;
            memcpy(out + len, c->pass, plen);
            len += plen;

            if (len > cap) return S5C_ERR_ARG;
            *out_len = len;
            c->state = S5C_ST_AUTH_SENT;
            return S5C_OK;
        } else {
            c->state = S5C_ST_ERROR;
            return S5C_ERR_AUTH;
        }
    } else if (c->state == S5C_ST_AUTH_SENT) {
        if (in_len < 2) return S5C_NEED_MORE;
        if (in[0] != 0x01) { c->state = S5C_ST_ERROR; return S5C_ERR_PROTO; }
        if (in[1] != 0x00) { c->state = S5C_ST_ERROR; return S5C_ERR_AUTH; }
        *consumed = 2;
        return format_connect(c, out, cap, out_len);
    } else if (c->state == S5C_ST_REQ_SENT) {
        if (in_len < 4) return S5C_NEED_MORE;
        if (in[0] != 0x05 || in[2] != 0x00) { c->state = S5C_ST_ERROR; return S5C_ERR_PROTO; }
        if (in[1] != 0x00) { c->state = S5C_ST_ERROR; return S5C_ERR_PROTO; }

        size_t req_len = 0;
        uint8_t atyp = in[3];
        if (atyp == 1) {
            req_len = 10;
        } else if (atyp == 3) {
            req_len = 7 + in[4];
        } else if (atyp == 4) {
            req_len = 22;
        } else {
            c->state = S5C_ST_ERROR;
            return S5C_ERR_PROTO;
        }

        if (in_len < req_len) return S5C_NEED_MORE;
        *consumed = req_len;
        c->state = S5C_ST_OPEN;
        return S5C_OK;
    }

    return S5C_ERR_PROTO;
}
