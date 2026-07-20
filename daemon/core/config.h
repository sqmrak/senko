#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* retain unknown transports so validation can return a stable reason */
typedef enum {
    VL_NET_TCP = 0,
    VL_NET_WS,
    VL_NET_GRPC,
    VL_NET_HTTP,
    VL_NET_XHTTP,
    VL_NET_UNKNOWN
} vl_net_t;

typedef enum {
    VL_SEC_NONE = 0,
    VL_SEC_TLS,
    VL_SEC_REALITY,
    VL_SEC_UNKNOWN
} vl_sec_t;

typedef enum {
    VL_PROTO_VLESS = 0,
    VL_PROTO_SOCKS5,
    VL_PROTO_HTTP,
    VL_PROTO_HTTPS
} vl_proto_t;

/* fixed storage avoids heap ownership ambiguity on ios 6 */
typedef struct {
    vl_proto_t proto;
    char     user[64];
    char     pass[64];

    char     uuid[64];
    char     host[256];
    uint16_t port;

    vl_sec_t security;
    vl_net_t net;

    char     sni[256];
    char     ws_host[256];
    char     flow[32];
    char     encryption[16];
    char     fp[32];
    char     pbk[128];
    char     sid[32];
    char     path[256];
    char     mode[16]; /* preserve xhttp mode for transport selection */
    char     remark[128];
} vl_server_t;

typedef enum {
    CFG_OK            =  0,
    CFG_ERR_BAD_ARG   = -1,
    CFG_ERR_SCHEME    = -2,
    CFG_ERR_NO_AT     = -3,
    CFG_ERR_NO_HOST   = -4,
    CFG_ERR_BAD_PORT  = -5,
    CFG_ERR_TOO_LONG  = -6,
    CFG_ERR_NO_MEMORY = -7
} cfg_status_t;

int url_percent_decode(const char *src, size_t src_len, char *dst, size_t cap);

/* keep names stable because the control protocol exposes them */
const char *vl_sec_name(vl_sec_t s);

cfg_status_t cfg_parse_link(const char *uri, vl_server_t *out);

/* validate after parsing so unsupported combinations fail explicitly */
int cfg_validate_server(const vl_server_t *s, char *reason, size_t reason_cap);

int cfg_validate_link(const char *uri, char *reason, size_t reason_cap);

/* skip malformed entries so one stale node does not hide valid nodes */
cfg_status_t cfg_parse_subscription(const char *blob, size_t blob_len,
                                    vl_server_t *out, size_t max_servers,
                                    size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* config_h */
