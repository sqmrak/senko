#ifndef SOCKS5_H
#define SOCKS5_H

#include <stddef.h>
#include <stdint.h>

#include "vless.h" /* reuse the vless destination representation */

#ifdef __cplusplus
extern "C" {
#endif

#define SOCKS5_VER          0x05

#define SOCKS5_ATYP_IPV4    0x01
#define SOCKS5_ATYP_DOMAIN  0x03
#define SOCKS5_ATYP_IPV6    0x04

#define SOCKS5_CMD_CONNECT  0x01
#define SOCKS5_CMD_BIND     0x02
#define SOCKS5_CMD_UDP_ASSOC 0x03

#define SOCKS5_REP_OK            0x00
#define SOCKS5_REP_GENERAL_FAIL  0x01
#define SOCKS5_REP_CMD_NOT_SUP   0x07
#define SOCKS5_REP_ATYP_NOT_SUP  0x08

typedef enum {
    S5_OK        =  0,
    S5_NEED_MORE =  1, /* wait for a complete request before consuming it */
    S5_ERR_VER   = -1, /* reject a non-socks5 byte stream */
    S5_ERR_ARG   = -2,
    S5_ERR_ATYP  = -3, /* reject an unsupported address type */
    S5_ERR_CMD   = -4 /* reject commands other than connect */
} s5_status_t;

/* consume greeting only after enough bytes arrive */
s5_status_t socks5_parse_greeting(const uint8_t *buf, size_t len,
                                  size_t *consumed);

s5_status_t socks5_build_method_reply(uint8_t *buf, size_t cap, size_t *out_len);

/* preserve the socks destination in vless form */
s5_status_t socks5_parse_request(const uint8_t *buf, size_t len,
                                 vless_dest_t *dest, size_t *consumed);

/* keep replies fixed because bound addresses are irrelevant here */
s5_status_t socks5_build_reply(uint8_t rep, uint8_t *buf, size_t cap,
                               size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* socks5_h */
