#include "socks5.h"
#include "net_safe.h"

#include <string.h>

s5_status_t socks5_parse_greeting(const uint8_t *buf, size_t len,
                                  size_t *consumed) {
    if (!buf || !consumed) return S5_ERR_ARG;
    if (len < 2) return S5_NEED_MORE; /* ver + nmethods */
    if (buf[0] != SOCKS5_VER) return S5_ERR_VER;

    uint8_t nmethods = buf[1];
    size_t need = (size_t)2 + nmethods;
    if (len < need) return S5_NEED_MORE; /* methods list incomplete */

    *consumed = need;
    return S5_OK;
}

s5_status_t socks5_build_method_reply(uint8_t *buf, size_t cap, size_t *out_len) {
    if (!buf || !out_len) return S5_ERR_ARG;
    if (cap < 2) return S5_ERR_ARG;
    buf[0] = SOCKS5_VER;
    buf[1] = 0x00; /* method: no authentication */
    *out_len = 2;
    return S5_OK;
}

s5_status_t socks5_parse_request(const uint8_t *buf, size_t len,
                                 vless_dest_t *dest, size_t *consumed) {
    if (!buf || !dest || !consumed) return S5_ERR_ARG;
    if (len < 4) return S5_NEED_MORE;
    if (buf[0] != SOCKS5_VER) return S5_ERR_VER;

    uint8_t cmd  = buf[1];
    uint8_t atyp = buf[3];

    if (cmd != SOCKS5_CMD_CONNECT) {
/* reject udp and bind because this client only carries tcp over vless */
        return S5_ERR_CMD;
    }

    memset(dest, 0, sizeof *dest);

    size_t off = 4; /* index of the address field */
    switch (atyp) {
        case SOCKS5_ATYP_IPV4: {
            if (len < off + 4 + 2) return S5_NEED_MORE;
            dest->atyp = VLESS_ADDR_IPV4; /* socks 1 -> vless 1 */
            memcpy(dest->host_addr, buf + off, 4);
            off += 4;
            break;
        }
        case SOCKS5_ATYP_IPV6: {
            if (len < off + 16 + 2) return S5_NEED_MORE;
            dest->atyp = VLESS_ADDR_IPV6; /* socks 4 -> vless 3 */
            memcpy(dest->host_addr, buf + off, 16);
            off += 16;
            break;
        }
        case SOCKS5_ATYP_DOMAIN: {
            if (len < off + 1) return S5_NEED_MORE; /* need the len byte */
            uint8_t dlen = buf[off];
            if (len < off + 1 + dlen + 2) return S5_NEED_MORE;
            dest->atyp = VLESS_ADDR_DOMAIN; /* socks 3 -> vless 2 */
            memcpy(dest->domain, buf + off + 1, dlen);
            dest->domain[dlen] = '\0';
            if (!net_hostname_safe(dest->domain)) return S5_ERR_ATYP;
            off += 1 + dlen;
            break;
        }
        default:
            return S5_ERR_ATYP;
    }

    dest->port = (uint16_t)((buf[off] << 8) | buf[off + 1]);
    off += 2;

    *consumed = off;
    return S5_OK;
}

s5_status_t socks5_build_reply(uint8_t rep, uint8_t *buf, size_t cap,
                               size_t *out_len) {
    if (!buf || !out_len) return S5_ERR_ARG;
    if (cap < 10) return S5_ERR_ARG;
    buf[0] = SOCKS5_VER;
    buf[1] = rep;
    buf[2] = 0x00;
    buf[3] = SOCKS5_ATYP_IPV4;
    memset(buf + 4, 0, 4); /* 0.0.0.0 */
    buf[8] = 0x00; buf[9] = 0x00; /* port 0 */
    *out_len = 10;
    return S5_OK;
}
