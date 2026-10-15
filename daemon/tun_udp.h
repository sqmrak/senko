#ifndef SENKO_TUN_UDP_H
#define SENKO_TUN_UDP_H

#include <stddef.h>
#include <stdint.h>

#include "core/dns_msg.h"
#include "core/config.h"
#include "core/tun_policy.h"
#include "core/transport.h"
#include "core/vless.h"
#include "direct_socket.h"
#include "tun_loop.h"
#include "tun_opener.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TUN_UDP_ASSOC_MAX 8
/* a burst of lookups waits here while the shared dns association opens */
#define TUN_UDP_QUEUE_MAX 8
#define TUN_UDP_CMD_OPENED (TUN_LOOP_CMD_OWNER + 2)

typedef struct {
    const transport_vt_t *vt; /* NULL with dial NULL: only direct traffic is served */
    transport_tls_cfg_t tls;
    vl_proto_t proto;      /* only vless carries udp; every protocol carries dns */
    uint8_t uuid[VLESS_UUID_LEN];
    const char *flow;
    const char *user;      /* borrowed, for the protocols that log in */
    const char *pass;
    tun_opener_dial_fn dial;
    void *dial_ctx;
    size_t opener_threads;
    tun_policy_t *policy;  /* borrowed; NULL sends everything through the server */
    direct_socket_fn direct_socket; /* NULL refuses traffic the rules send direct */
    void *direct_ctx;
    dns_block_response_t block_response;
    uint8_t dns_upstream[16];
    uint8_t dns_upstream_len;
    int use_shared_tls;
} tun_udp_config_t;

typedef struct {
    uint64_t received;
    uint64_t sent;
    uint64_t refused;
    uint64_t invalid_responses;
    uint64_t expired;
    uint64_t direct;       /* datagrams that left by the physical interface */
    uint64_t blocked;      /* datagrams a rule dropped */
    uint64_t dns_timeouts; /* lookups the upstream never answered */
    char last_error[256];
} tun_udp_stats_t;

typedef struct tun_udp tun_udp_t;

size_t tun_udp_size(void);

/* installs beside tun_tcp after tun_tcp_init, or alone in a loop */
int tun_udp_init(tun_udp_t *udp, const tun_udp_config_t *config,
                 tun_loop_config_t *loop_config);
void tun_udp_bind(tun_udp_t *udp, tun_loop_t *loop, uint64_t generation);
const tun_udp_stats_t *tun_udp_stats(const tun_udp_t *udp);

#ifdef __cplusplus
}
#endif

#endif
