#ifndef VLESS_CONN_H
#define VLESS_CONN_H

#include <stddef.h>
#include <stdint.h>

#include "vless.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VC_ST_INIT = 0, /* hold state before the request is emitted */
    VC_ST_REQ_SENT, /* wait for the response header */
    VC_ST_OPEN, /* relay bytes after the response header */
    VC_ST_ERROR /* stop after a protocol violation */
} vc_state_t;

typedef enum {
    VC_OK        =  0,
    VC_NEED_MORE =  1, /* retain partial response data */
    VC_ERR_ARG   = -1,
    VC_ERR_STATE = -2, /* reject an operation for the current state */
    VC_ERR_PROTO = -3 /* reject a malformed response header */
} vc_status_t;

#define VC_RESP_HDR_MAX  257

typedef struct {
    vc_state_t  state;

    uint8_t     uuid[VLESS_UUID_LEN];
    vless_dest_t dest;
    const char *flow; /* borrow the flow from the server configuration */

    uint8_t     rbuf[VC_RESP_HDR_MAX];
    size_t      rbuf_len;
} vless_conn_t;

/* copy request identity while borrowing the stable flow configuration */
vc_status_t vless_conn_init(vless_conn_t *c,
                            const uint8_t uuid[VLESS_UUID_LEN],
                            const vless_dest_t *dest,
                            const char *flow);

/* emit the request and first payload together when vision requires atomicity */
vc_status_t vless_conn_make_request(vless_conn_t *c,
                                    const uint8_t *payload, size_t payload_len,
                                    uint8_t *out, size_t cap, size_t *out_len);

/* consume the response header and report where application data begins */
vc_status_t vless_conn_feed_response(vless_conn_t *c,
                                     const uint8_t *in, size_t in_len,
                                     size_t *app_off);

#ifdef __cplusplus
}
#endif

#endif /* vless_conn_h */
