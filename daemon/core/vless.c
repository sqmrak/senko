#include "vless.h"

#include <string.h>

static size_t bounded_len(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) ++n;
    return n;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

vless_status_t vless_uuid_parse(const char *str, uint8_t out[VLESS_UUID_LEN]) {
    if (!str || !out) return VLESS_ERR_BAD_ARG;

    int n = 0; /* nibbles seen so far */
    int hi = 0; /* pending high nibble */
    for (const char *p = str; *p; ++p) {
        if (*p == '-') continue; /* keep formatting noise out */
        int v = hexval(*p);
        if (v < 0) return VLESS_ERR_BAD_UUID; /* invalid hex breaks the id */
        if (n >= VLESS_UUID_LEN * 2) return VLESS_ERR_BAD_UUID; /* too long */
        if ((n & 1) == 0) {
            hi = v; /* hold the high nibble until paired */
        } else {
            out[n / 2] = (uint8_t)((hi << 4) | v);
        }
        ++n;
    }
    if (n != VLESS_UUID_LEN * 2) return VLESS_ERR_BAD_UUID; /* need 32 nibbles */
    return VLESS_OK;
}

static int addr_wire_len(const vless_dest_t *d) {
    switch (d->atyp) {
        case VLESS_ADDR_IPV4: return 4;
        case VLESS_ADDR_IPV6: return 16;
        case VLESS_ADDR_DOMAIN: {
            size_t l = bounded_len(d->domain, sizeof(d->domain));
            if (l == 0 || l > 255) return -1; /* domain must fit one length byte */
            return (int)(1 + l); /* length byte plus host bytes */
        }
        default: return -1;
    }
}

vless_status_t vless_build_request(const vless_request_t *req,
                                   uint8_t *buf, size_t cap, size_t *out_len) {
    if (!req || !buf || !out_len) return VLESS_ERR_BAD_ARG;

    int alen = addr_wire_len(&req->dest);
    if (alen < 0) return VLESS_ERR_BAD_ADDR;

/* only emit flow when present so the addon block stays minimal */
    size_t flow_len = (req->flow && req->flow[0]) ? strlen(req->flow) : 0;
    if (flow_len > 255) return VLESS_ERR_BAD_ARG; /* flow should stay short */
    size_t addons_len = flow_len ? (2 + flow_len) : 0;

    size_t need = 1 + VLESS_UUID_LEN + 1 + addons_len + 1 + 2 + 1 + (size_t)alen;
    if (cap < need) return VLESS_ERR_BUF_TOO_SMALL;

    uint8_t *p = buf;
    *p++ = VLESS_VERSION;

    memcpy(p, req->uuid, VLESS_UUID_LEN);
    p += VLESS_UUID_LEN;

    *p++ = (uint8_t)addons_len;
    if (addons_len) {
        *p++ = 0x0a; /* flow is encoded as a string field */
        *p++ = (uint8_t)flow_len;
        memcpy(p, req->flow, flow_len);
        p += flow_len;
    }

    *p++ = (uint8_t)req->cmd;

    *p++ = (uint8_t)(req->dest.port >> 8); /* ports are serialized big endian */
    *p++ = (uint8_t)(req->dest.port & 0xff);

    *p++ = (uint8_t)req->dest.atyp;
    switch (req->dest.atyp) {
        case VLESS_ADDR_IPV4:
            memcpy(p, req->dest.host_addr, 4); p += 4;
            break;
        case VLESS_ADDR_IPV6:
            memcpy(p, req->dest.host_addr, 16); p += 16;
            break;
        case VLESS_ADDR_DOMAIN: {
            size_t l = bounded_len(req->dest.domain, sizeof(req->dest.domain));
            *p++ = (uint8_t)l; /* domain payload starts with length */
            memcpy(p, req->dest.domain, l); p += l;
            break;
        }
        default:
            return VLESS_ERR_BAD_ADDR; /* addr_wire_len already filters this */
    }

    *out_len = (size_t)(p - buf);
    return VLESS_OK;
}

vless_status_t vless_parse_response(const uint8_t *buf, size_t len,
                                    size_t *out_hdr_len) {
    if (!buf || !out_hdr_len) return VLESS_ERR_BAD_ARG;

    if (len < 2) return VLESS_ERR_SHORT;

    if (buf[0] != VLESS_VERSION) return VLESS_ERR_BAD_VERSION;

    uint8_t addons_len = buf[1];
    size_t hdr = (size_t)2 + addons_len;
    if (len < hdr) return VLESS_ERR_SHORT; /* wait for the full addon block */

/* response addons carry no action here, so skip them and return payload */
    *out_hdr_len = hdr;
    return VLESS_OK;
}
