#ifndef REALITY_AUTH_H
#define REALITY_AUTH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RA_AUTHKEY_LEN   32
#define RA_SESSIONID_LEN 32
#define RA_TOKEN_PT_LEN  16 /* keep the sealed token payload fixed */
#define RA_RANDOM_LEN    32 /* match the tls client random size */
#define RA_SHORTID_MAX   8

typedef enum {
    RA_OK         =  0,
    RA_ERR_ARG    = -1,
    RA_ERR_CRYPTO = -2, /* report failures from the crypto primitive */
    RA_ERR_AUTH   = -3 /* reject invalid tokens or server proofs */
} ra_status_t;

/* bind the client random and server key to the reality authentication key */
ra_status_t reality_derive_authkey(const uint8_t our_priv[RA_AUTHKEY_LEN],
                                   const uint8_t their_pub[RA_AUTHKEY_LEN],
                                   const uint8_t client_random[RA_RANDOM_LEN],
                                   uint8_t authkey[RA_AUTHKEY_LEN]);

/* seal the fixed token into the clienthello session id using its zeroed aad */
ra_status_t reality_seal_token(const uint8_t authkey[RA_AUTHKEY_LEN],
                               const uint8_t client_random[RA_RANDOM_LEN],
                               const uint8_t version[3], uint32_t unix_time,
                               const uint8_t *short_id, size_t short_id_len,
                               const uint8_t *aad, size_t aad_len,
                               uint8_t out_session_id[RA_SESSIONID_LEN]);

/* open a token in tests and reject any key, aad, or tag mismatch */
ra_status_t reality_open_token(const uint8_t authkey[RA_AUTHKEY_LEN],
                               const uint8_t client_random[RA_RANDOM_LEN],
                               const uint8_t *aad, size_t aad_len,
                               const uint8_t session_id[RA_SESSIONID_LEN],
                               uint8_t out_plaintext[RA_TOKEN_PT_LEN]);

/* verify the endpoint proof before accepting a fronted server response */
ra_status_t reality_verify_server(const uint8_t authkey[RA_AUTHKEY_LEN],
                                  const uint8_t *server_ed25519_pub, size_t pub_len,
                                  const uint8_t *signature, size_t sig_len);

#ifdef __cplusplus
}
#endif

#endif /* reality_auth_h */
