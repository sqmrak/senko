#ifndef AWG_TUNNEL_H
#define AWG_TUNNEL_H

#include <stddef.h>
#include <stdint.h>

#include "awg_handshake.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AWG_TUN_OK = 0,
    AWG_TUN_ERR_ARG = -1,
    AWG_TUN_ERR_SPACE = -2,
    AWG_TUN_ERR_FORMAT = -3,
    AWG_TUN_ERR_AUTH = -4,
    AWG_TUN_ERR_REPLAY = -5,
    AWG_TUN_ERR_CRYPTO = -6
} awg_tun_status_t;

typedef struct {
    awg_handshake_t handshake;
    uint64_t send_counter;
    uint64_t recv_counter;
    uint64_t recv_window;
    int have_recv_counter;
} awg_tunnel_t;

void awg_tunnel_init(awg_tunnel_t *tunnel, const awg_config_t *cfg);

/* protect one ip packet after the peer authenticated the handshake */
awg_tun_status_t awg_tunnel_seal(awg_tunnel_t *tunnel,
                                 const uint8_t *packet, size_t packet_len,
                                 uint8_t *out, size_t cap, size_t *out_len);

/* reject stale counters before an ip packet reaches the local interface */
awg_tun_status_t awg_tunnel_open(awg_tunnel_t *tunnel,
                                 const uint8_t *packet, size_t packet_len,
                                 uint8_t *out, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
