#ifndef SENKO_TUN_TCP_H
#define SENKO_TUN_TCP_H

#include <stddef.h>
#include <stdint.h>

#include "core/session.h"
#include "core/transport.h"
#include "core/tun_policy.h"
#include "direct_socket.h"
#include "tun_loop.h"
#include "tun_opener.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the bridge between a captured tcp flow and senko's own protocol core.

   the app's side is the flow's byte stream in daemon/tun_stack.h. the
   server's side is a session from core/session.h over a transport, the same
   session the socks frontend in daemon/loop.c drives, so vless, vision and
   the transports are not written a second time.

   a new flow is held while its server connection opens on the opener pool.
   a socks or http proxy must also answer CONNECT before the app's handshake
   completes; a failure answers the app with a reset. no byte of the app ever
   leaves any other way than through that server connection

   everything here runs on the tunnel's owning thread, driven by the hooks and
   callbacks tun_tcp_init() installs in the loop configuration */

/* bytes taken from the session that the app's window could not take yet */
#define TUN_TCP_HOLD 4096
#define TUN_TCP_VERIFY_MS 8000
#define TUN_TCP_DEFAULT_MAX_FLOWS 8
#define TUN_TCP_CMD_OPENED (TUN_LOOP_CMD_OWNER + 1)

typedef struct {
    const transport_vt_t *vt;    /* NULL with dial NULL: only direct flows are served */
    transport_tls_cfg_t   tls;   /* borrowed: the strings must outlive the bridge */
    vl_proto_t            proto;
    uint8_t               uuid[VLESS_UUID_LEN];
    const char           *flow;  /* vless flow, e.g. vision; borrowed, may be NULL */
    const char           *user;
    const char           *pass;
    tun_opener_dial_fn    dial;
    void                 *dial_ctx;
    size_t                opener_threads;
    size_t                max_flows; /* 0 uses a bounded low-memory default */
    int                   use_shared_tls; /* TLS transports reuse one trust store */
    tun_policy_t         *policy;   /* borrowed; NULL sends every flow through the server */
    direct_socket_fn      direct_socket; /* NULL refuses flows the rules send direct */
    void                 *direct_ctx;
    uint8_t               dns_upstream[16];
    uint8_t               dns_upstream_len; /* 0 preserves the captured TCP/53 target */
} tun_tcp_config_t;

typedef struct {
    uint64_t requested;
    uint64_t opened;         /* server connection ready, app handshake allowed */
    uint64_t refused;        /* no server connection: the app got a reset */
    uint64_t aborted;        /* a running flow failed: the app got a reset */
    uint64_t finished;       /* both sides closed cleanly */
    uint64_t direct;         /* flows the rules sent out of the physical interface */
    uint64_t blocked;        /* flows a rule answered with a reset */
    uint64_t bytes_to_server;
    uint64_t bytes_to_app;
    char     last_error[256];
} tun_tcp_stats_t;

typedef enum {
    TUN_TCP_OK       =  0,
    TUN_TCP_ERR_ARG  = -1
} tun_tcp_status_t;

typedef struct tun_tcp tun_tcp_t;

size_t tun_tcp_size(void);

/* installs the bridge in a loop configuration before tun_loop_init() */
tun_tcp_status_t tun_tcp_init(tun_tcp_t *tcp, const tun_tcp_config_t *config,
                              tun_loop_config_t *loop_config);

/* after tun_loop_init(): the opener pool wakes the loop through it */
void tun_tcp_bind(tun_tcp_t *tcp, tun_loop_t *loop, uint64_t generation);

/* readable on the loop thread, or by anyone once the loop has ended */
const tun_tcp_stats_t *tun_tcp_stats(const tun_tcp_t *tcp);

#ifdef __cplusplus
}
#endif

#endif
