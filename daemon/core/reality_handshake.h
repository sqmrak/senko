#ifndef REALITY_HANDSHAKE_H
#define REALITY_HANDSHAKE_H

#include <stddef.h>
#include <stdint.h>

#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RH_OK              =  0,
    RH_ERR_ARG         = -1,
    RH_ERR_IO          = -2, /* report socket progress or failure */
    RH_ERR_PROTO       = -3, /* reject unexpected tls bytes */
    RH_ERR_CRYPTO      = -4, /* report a failed crypto primitive */
    RH_ERR_SUITE       = -5, /* reject an unsupported cipher suite */
    RH_ERR_NOT_REALITY = -6 /* reject a fronting site without proof */
} rh_status_t;

typedef struct {
    const char *sni; /* present the configured fronting name */
    uint8_t     pbk[32]; /* retain the decoded server public key */
    uint8_t     short_id[8]; /* retain the zero-padded short id */
    size_t      short_id_len; /* preserve the significant short-id bytes */
    uint8_t     version[3]; /* stamp the client version into the token */
    int         fp; /* avoid an include cycle with tls fingerprints */
    uint8_t     p256_pub[65]; /* add the firefox decoy share when available */
    int         has_p256;
} rh_params_t;

/* run the blocking bootstrap and return a handle without taking fd ownership */
void *reality_handshake_open(int fd, const rh_params_t *p, rh_status_t *err);

extern const transport_vt_t transport_reality;

#ifdef __cplusplus
}
#endif

#endif /* reality_handshake_h */
