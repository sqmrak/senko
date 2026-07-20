#ifndef TLS_CLIENTHELLO_H
#define TLS_CLIENTHELLO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLS_CH_RANDOM_LEN     32
#define TLS_CH_SESSIONID_LEN  32
#define TLS_CH_X25519_PUB_LEN 32
#define TLS_CH_SESSIONID_OFF  39 /* keep reality aad offsets deterministic */

typedef enum {
    TLS_CH_OK        =  0,
    TLS_CH_ERR_ARG   = -1,
    TLS_CH_ERR_SPACE = -2, /* reject output beyond fixed storage */
    TLS_CH_ERR_SNI   = -3 /* reject an sni beyond fixed storage */
} tls_ch_status_t;

/* model browser fingerprints while keeping the supported tls suites narrow */
typedef enum {
    TLS_FP_CHROME = 0, /* use chrome when no profile is configured */
    TLS_FP_EDGE,
    TLS_FP_FIREFOX,
    TLS_FP_QQ,
    TLS_FP_RANDOMIZED
} tls_fp_t;

/* carry caller-owned random, session, key-share, sni, and fingerprint inputs */
typedef struct {
    uint8_t     random[TLS_CH_RANDOM_LEN];
    uint8_t     session_id[TLS_CH_SESSIONID_LEN];
    uint8_t     x25519_pub[TLS_CH_X25519_PUB_LEN];
    const char *sni; /* present the configured fronting name */
    tls_fp_t    fp; /* select the browser-shaped hello */
    const uint8_t *p256_pub; /* add the firefox decoy share when available */
} tls_ch_params_t;

/* serialize the exact hello bytes used by reality aad and tls transcript */
tls_ch_status_t tls_build_clienthello(const tls_ch_params_t *p,
                                      uint8_t *buf, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* tls_clienthello_h */
