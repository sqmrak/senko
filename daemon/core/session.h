#ifndef SESSION_H
#define SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "transport.h"
#include "config.h"
#include "vless.h"
#include "vless_conn.h"
#include "vision.h"
#include "socks5_client.h"
#include "http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SESS_GREETING = 0, /* wait for socks greeting before opening remote work */
    SESS_REQUEST, /* need destination before sending vless */
    SESS_VISION_FIRST, /* delay the first write for vision bootstrap safety */
    SESS_VLESS_RESP, /* wait for the remote response header */
    SESS_RELAY, /* relay application bytes in both directions */
    SESS_CLOSED, /* drain client output after remote eof */
    SESS_ERROR /* close both sides after a protocol failure */
} sess_state_t;

typedef enum {
    SESS_OK      =  0,
    SESS_ERR_ARG = -1,
    SESS_ERR     = -2 /* move the session to the terminal error state */
} sess_status_t;

/* keep enough client output queued to avoid truncating package downloads */
#define SESS_BUF  (64 * 1024)

typedef struct {
    sess_state_t state;

    const transport_vt_t *vt; /* borrow transport operations from the loop */
    void                 *th; /* borrow the loop-owned transport handle */

    vl_proto_t            proto;
    union {
        vless_conn_t    vc;
        socks5_client_t s5c;
        http_client_t   hc;
    } u;

    uint8_t client_stage[512];
    size_t  client_stage_len;

/* queue local output because socket writes can apply backpressure */
    uint8_t to_client[SESS_BUF];
    size_t  to_client_len;

/* queue remote output when transport_write reports backpressure */
    uint8_t to_remote[SESS_BUF];
    size_t  to_remote_len;
    int     to_remote_raw;

/* track vision framing so direct mode can splice raw bytes safely */
    int            vision_on;
    int            vision_upstream_direct;
    int            vision_upstream_direct_pending;
    int            vision_upstream_direct_mark_transport;
    int            vision_downstream_direct;
    long           vision_first_deadline_ms;
    vision_wrap_t  vwrap;
    vision_unpad_t vunpad;

/* delay socks success until the remote response proves the tunnel exists */
    int            pending_socks_ok;

#ifndef SENKO_RELEASE
    char     trace_host[280];
    uint64_t trace_app_tx;
    uint64_t trace_app_rx;
    uint64_t trace_wire_tx;
    uint64_t trace_wire_rx;
    int      trace_closed;

    uint8_t  replay_buf[64 * 1024];
    size_t   replay_len;
    uint16_t replay_chunks;
    uint8_t  replay_uuid[16];
    int      replay_uuid_set;

    uint8_t  upload_in_buf[64 * 1024];
    size_t   upload_in_len;
    uint32_t upload_in_chunks;
    uint8_t  upload_wire_buf[64 * 1024];
    size_t   upload_wire_len;
    uint32_t upload_wire_chunks;
    uint8_t  upload_uuid[16];
    int      upload_uuid_set;
    int      upload_started;
#endif
} session_t;

/* initialize a session over an already-open transport handle */
sess_status_t session_init(session_t *s,
                           const transport_vt_t *vt, void *th,
                           vl_proto_t proto,
                           const uint8_t uuid[VLESS_UUID_LEN],
                           const char *flow,
                           const char *user,
                           const char *pass);

/* greeting reply already went out while the transport was still opening */
sess_status_t session_init_after_greet(session_t *s,
                                       const transport_vt_t *vt, void *th,
                                       vl_proto_t proto,
                                       const uint8_t uuid[VLESS_UUID_LEN],
                                       const char *flow,
                                       const char *user,
                                       const char *pass);

/* attach destination after transport open so early payload stays ordered */
sess_status_t session_start_from_socks_dest(session_t *s,
                                            const vless_dest_t *dest,
                                            const uint8_t *payload,
                                            size_t payload_len);

/* pf-transparent clients: no socks5 reply on the local socket */
sess_status_t session_start_from_transparent_dest(session_t *s,
                                                  const vless_dest_t *dest,
                                                  const uint8_t *payload,
                                                  size_t payload_len,
                                                  size_t *payload_used_out);

/* feed local bytes and report consumed input under backpressure */
sess_status_t session_feed_client(session_t *s,
                                  const uint8_t *in, size_t len,
                                  size_t *consumed);

/* pump remote bytes and queue client output without blocking */
sess_status_t session_pump_remote(session_t *s);

/* drain queued client output without exposing the internal buffer */
size_t session_take_client(session_t *s, uint8_t *out, size_t cap);

/* close only after errors or a drained remote eof */
int session_is_done(const session_t *s);

#ifdef __cplusplus
}
#endif

#endif /* session_h */
