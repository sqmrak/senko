#ifndef AWG_HANDSHAKE_H
#define AWG_HANDSHAKE_H

#include <stddef.h>
#include <stdint.h>

#include "awg_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AWG_INIT_PACKET_LEN 148
#define AWG_RESP_PACKET_LEN 92
#define AWG_DATAGRAM_MAX 65535

typedef enum {
    AWG_HS_OK = 0,
    AWG_HS_ERR_ARG = -1,
    AWG_HS_ERR_CRYPTO = -2,
    AWG_HS_ERR_SPACE = -3,
    AWG_HS_ERR_FORMAT = -4,
    AWG_HS_ERR_AUTH = -5,
    AWG_HS_ERR_IO = -6,
    AWG_HS_ERR_TIMEOUT = -7
} awg_hs_status_t;

typedef struct {
    const awg_config_t *cfg;
    uint8_t chain_key[32];
    uint8_t hash[32];
    uint8_t ephemeral_private[32];
    uint8_t ephemeral_public[32];
    uint8_t send_key[32];
    uint8_t recv_key[32];
    uint32_t sender_index;
    uint32_t receiver_index;
    uint32_t received_datagrams;
    uint32_t ignored_datagrams;
    uint32_t last_unrelated_type;
    uint32_t last_unrelated_len;
    int debug_stage;
    int established;
} awg_handshake_t;

void awg_handshake_init(awg_handshake_t *hs, const awg_config_t *cfg);

int awg_blake2s_keyed(const uint8_t *key, size_t key_len,
                      const uint8_t *input, size_t input_len, uint8_t out[32]);
int awg_blake2s_keyed_16(const uint8_t *key, size_t key_len,
                         const uint8_t *input, size_t input_len, uint8_t out[16]);

/* emit the first authenticated packet after awg-specific prefix padding */
awg_hs_status_t awg_handshake_build_initiation(awg_handshake_t *hs,
                                                uint8_t *out, size_t cap,
                                                size_t *out_len);

/* accept only the response tied to the initiating sender index */
awg_hs_status_t awg_handshake_consume_response(awg_handshake_t *hs,
                                                const uint8_t *packet, size_t packet_len);

/* prove endpoint compatibility before the backend changes any system route */
awg_hs_status_t awg_handshake_probe(const awg_config_t *cfg, int timeout_ms,
                                    char *reason, size_t reason_cap);

/* retain accepted keys for the packet engine after the endpoint preflight */
awg_hs_status_t awg_handshake_establish_fd(int fd, const awg_config_t *cfg,
                                           int timeout_ms, awg_handshake_t *hs,
                                           char *reason, size_t reason_cap);

/* expand an i field without sending it so callers can bound generated traffic */
awg_hs_status_t awg_signature_expand(const char *spec, uint8_t *out, size_t cap,
                                     size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
