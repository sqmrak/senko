#ifndef VLESS_H
#define VLESS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VLESS_VERSION       0x00
#define VLESS_UUID_LEN      16

typedef enum {
    VLESS_CMD_TCP = 0x01,
    VLESS_CMD_UDP = 0x02,
    VLESS_CMD_MUX = 0x03
} vless_cmd_t;

typedef enum {
    VLESS_ADDR_IPV4   = 0x01,
    VLESS_ADDR_DOMAIN = 0x02,
    VLESS_ADDR_IPV6   = 0x03
} vless_atyp_t;

typedef enum {
    VLESS_OK              =  0,
    VLESS_ERR_BUF_TOO_SMALL = -1, /* reject a header beyond fixed storage */
    VLESS_ERR_BAD_ARG     = -2, /* reject a missing or invalid argument */
    VLESS_ERR_BAD_UUID    = -3, /* reject an invalid uuid string */
    VLESS_ERR_BAD_ADDR    = -4, /* reject an invalid destination */
    VLESS_ERR_SHORT       = -5, /* wait for the complete header */
    VLESS_ERR_BAD_VERSION = -6 /* reject a response with wrong version */
} vless_status_t;

typedef struct {
    vless_atyp_t atyp;
/* keep one destination layout for ip, ipv6, and domain requests */
    uint8_t  host_addr[16];
    char     domain[256];
    uint16_t port; /* store host order until wire serialization */
} vless_dest_t;

typedef struct {
    uint8_t      uuid[VLESS_UUID_LEN];
    vless_cmd_t  cmd;
    vless_dest_t dest;
    const char  *flow;
} vless_request_t;

/* avoid depending on libc uuid helpers on old ios */
vless_status_t vless_uuid_parse(const char *str, uint8_t out[VLESS_UUID_LEN]);

/* keep payload ownership outside the vless header encoder */
vless_status_t vless_build_request(const vless_request_t *req,
                                   uint8_t *buf, size_t cap, size_t *out_len);

/* leave response payload queued for the caller to relay */
vless_status_t vless_parse_response(const uint8_t *buf, size_t len,
                                    size_t *out_hdr_len);

#ifdef __cplusplus
}
#endif

#endif /* vless_h */
