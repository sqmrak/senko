#include "reality_auth.h"
#include "reality_crypto.h"

#include <string.h>

ra_status_t reality_derive_authkey(const uint8_t our_priv[RA_AUTHKEY_LEN],
                                   const uint8_t their_pub[RA_AUTHKEY_LEN],
                                   const uint8_t client_random[RA_RANDOM_LEN],
                                   uint8_t authkey[RA_AUTHKEY_LEN]) {
    if (!our_priv || !their_pub || !client_random || !authkey) return RA_ERR_ARG;

    uint8_t shared[RC_SHARED_LEN];
    if (rc_x25519_shared(our_priv, their_pub, shared) != RC_OK) return RA_ERR_CRYPTO;

/* derive the reality auth key from the exact wire salt and info value */
    static const uint8_t info[] = { 'R', 'E', 'A', 'L', 'I', 'T', 'Y' };
    ra_status_t rc = (rc_hkdf_sha256(shared, sizeof shared,
                                     client_random, 20,
                                     info, sizeof info,
                                     authkey, RA_AUTHKEY_LEN) == RC_OK)
                     ? RA_OK : RA_ERR_CRYPTO;
    memset(shared, 0, sizeof shared);
    return rc;
}

static void build_token(uint8_t pt[RA_TOKEN_PT_LEN],
                        const uint8_t version[3], uint32_t unix_time,
                        const uint8_t *short_id, size_t short_id_len) {
    memset(pt, 0, RA_TOKEN_PT_LEN);
    pt[0] = version[0];
    pt[1] = version[1];
    pt[2] = version[2];
    pt[3] = 0; /* reserved */
    pt[4] = (uint8_t)(unix_time >> 24); /* big-endian unix seconds */
    pt[5] = (uint8_t)(unix_time >> 16);
    pt[6] = (uint8_t)(unix_time >> 8);
    pt[7] = (uint8_t)(unix_time);
    size_t n = short_id_len > RA_SHORTID_MAX ? RA_SHORTID_MAX : short_id_len;
    if (n && short_id) memcpy(pt + 8, short_id, n);
}

ra_status_t reality_seal_token(const uint8_t authkey[RA_AUTHKEY_LEN],
                               const uint8_t client_random[RA_RANDOM_LEN],
                               const uint8_t version[3], uint32_t unix_time,
                               const uint8_t *short_id, size_t short_id_len,
                               const uint8_t *aad, size_t aad_len,
                               uint8_t out_session_id[RA_SESSIONID_LEN]) {
    if (!authkey || !client_random || !version || !out_session_id) return RA_ERR_ARG;
    if (aad_len && !aad) return RA_ERR_ARG;

    uint8_t pt[RA_TOKEN_PT_LEN];
    build_token(pt, version, unix_time, short_id, short_id_len);

/* seal the token against the zeroed clienthello used as aad */
    uint8_t tag[RC_GCM_TAGLEN];
    if (rc_aes256gcm_seal(authkey, client_random + 20,
                          aad, aad_len,
                          pt, RA_TOKEN_PT_LEN,
                          out_session_id, tag) != RC_OK) {
        memset(pt, 0, sizeof pt);
        return RA_ERR_CRYPTO;
    }
    memcpy(out_session_id + RA_TOKEN_PT_LEN, tag, RC_GCM_TAGLEN); /* [16:32] */
    memset(pt, 0, sizeof pt);
    return RA_OK;
}

ra_status_t reality_open_token(const uint8_t authkey[RA_AUTHKEY_LEN],
                               const uint8_t client_random[RA_RANDOM_LEN],
                               const uint8_t *aad, size_t aad_len,
                               const uint8_t session_id[RA_SESSIONID_LEN],
                               uint8_t out_plaintext[RA_TOKEN_PT_LEN]) {
    if (!authkey || !client_random || !session_id || !out_plaintext) return RA_ERR_ARG;
    if (aad_len && !aad) return RA_ERR_ARG;

    const uint8_t *ct = session_id;
    const uint8_t *tag = session_id + RA_TOKEN_PT_LEN;
    if (rc_aes256gcm_open(authkey, client_random + 20,
                          aad, aad_len,
                          ct, RA_TOKEN_PT_LEN, tag,
                          out_plaintext) != RC_OK) {
        return RA_ERR_AUTH; /* tag mismatch: wrong key / tampered / bad aad */
    }
    return RA_OK;
}

/* constant-time compare; reality's match is security-sensitive so don't bail early on the first differing byte */
static int ct_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

ra_status_t reality_verify_server(const uint8_t authkey[RA_AUTHKEY_LEN],
                                  const uint8_t *server_ed25519_pub, size_t pub_len,
                                  const uint8_t *signature, size_t sig_len) {
    if (!authkey || !server_ed25519_pub || !signature) return RA_ERR_ARG;
    if (sig_len != 64) return RA_ERR_AUTH;

    uint8_t mac[64];
    if (rc_hmac_sha512(authkey, RA_AUTHKEY_LEN,
                       server_ed25519_pub, pub_len, mac) != RC_OK)
        return RA_ERR_CRYPTO;

    ra_status_t rc = ct_equal(mac, signature, 64) ? RA_OK : RA_ERR_AUTH;
    memset(mac, 0, sizeof mac);
    return rc;
}
