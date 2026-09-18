#include "shadowsocks_client.h"
#include "reality_crypto.h"

#include <string.h>
#include <strings.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

ss_cipher_t ss_cipher_from_name(const char *name) {
    if (!name || !name[0]) return SS_CIPHER_UNKNOWN;
    if (strcasecmp(name, "aes-256-gcm") == 0) return SS_CIPHER_AES_256_GCM;
    if (strcasecmp(name, "aes-128-gcm") == 0) return SS_CIPHER_AES_128_GCM;
    if (strcasecmp(name, "chacha20-ietf-poly1305") == 0 ||
        strcasecmp(name, "chacha20-poly1305") == 0)
        return SS_CIPHER_CHACHA20_POLY1305;
    return SS_CIPHER_UNKNOWN;
}

const char *ss_cipher_to_name(ss_cipher_t c) {
    switch (c) {
        case SS_CIPHER_AES_256_GCM: return "aes-256-gcm";
        case SS_CIPHER_AES_128_GCM: return "aes-128-gcm";
        case SS_CIPHER_CHACHA20_POLY1305: return "chacha20-ietf-poly1305";
        default: return "unknown";
    }
}

static void ss_inc_nonce(uint8_t nonce[12]) {
    for (int i = 0; i < 12; ++i) {
        if (++nonce[i] != 0) break;
    }
}

static int ss_hkdf_sha1(const uint8_t *ikm, size_t ikm_len,
                        const uint8_t *salt, size_t salt_len,
                        const uint8_t *info, size_t info_len,
                        uint8_t *out, size_t out_len) {
    uint8_t prk[20];
    unsigned int prk_len = 0;
    static const uint8_t zero_salt[20] = {0};
    if (!salt || salt_len == 0) {
        salt = zero_salt;
        salt_len = sizeof zero_salt;
    }
    if (!HMAC(EVP_sha1(), salt, (int)salt_len, ikm, ikm_len, prk, &prk_len) || prk_len != 20)
        return -1;

    uint8_t t[20];
    unsigned int t_len = 0;
    size_t generated = 0;
    uint8_t counter = 1;

    while (generated < out_len) {
        HMAC_CTX *hctx = HMAC_CTX_new();
        if (!hctx) return -1;
        if (!HMAC_Init_ex(hctx, prk, 20, EVP_sha1(), NULL)) {
            HMAC_CTX_free(hctx);
            return -1;
        }
        if (counter > 1) {
            HMAC_Update(hctx, t, t_len);
        }
        if (info && info_len > 0) {
            HMAC_Update(hctx, info, info_len);
        }
        HMAC_Update(hctx, &counter, 1);
        HMAC_Final(hctx, t, &t_len);
        HMAC_CTX_free(hctx);

        size_t to_copy = out_len - generated;
        if (to_copy > t_len) to_copy = t_len;
        memcpy(out + generated, t, to_copy);
        generated += to_copy;
        counter++;
    }
    return 0;
}

static int ss_derive_master_key(const char *pass, size_t pass_len, uint8_t *key, size_t key_len) {
    uint8_t md[16];
    size_t offset = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;
    while (offset < key_len) {
        EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
        if (offset > 0) EVP_DigestUpdate(ctx, md, 16);
        EVP_DigestUpdate(ctx, pass, pass_len);
        EVP_DigestFinal_ex(ctx, md, NULL);
        size_t copy = key_len - offset;
        if (copy > 16) copy = 16;
        memcpy(key + offset, md, copy);
        offset += copy;
    }
    EVP_MD_CTX_free(ctx);
    return 0;
}

static int ss_aead_seal(ss_cipher_t cipher, const uint8_t *key, const uint8_t nonce[12],
                        const uint8_t *pt, size_t pt_len, uint8_t *ct, uint8_t tag[16]) {
    if (cipher == SS_CIPHER_AES_256_GCM)
        return rc_aes256gcm_seal(key, nonce, NULL, 0, pt, pt_len, ct, tag) == RC_OK ? 0 : -1;
    if (cipher == SS_CIPHER_AES_128_GCM)
        return rc_aes128gcm_seal(key, nonce, NULL, 0, pt, pt_len, ct, tag) == RC_OK ? 0 : -1;
    if (cipher == SS_CIPHER_CHACHA20_POLY1305)
        return rc_chacha20poly1305_seal(key, nonce, NULL, 0, pt, pt_len, ct, tag) == RC_OK ? 0 : -1;
    return -1;
}

static int ss_aead_open(ss_cipher_t cipher, const uint8_t *key, const uint8_t nonce[12],
                        const uint8_t *ct, size_t ct_len, const uint8_t tag[16], uint8_t *pt) {
    if (cipher == SS_CIPHER_AES_256_GCM)
        return rc_aes256gcm_open(key, nonce, NULL, 0, ct, ct_len, tag, pt) == RC_OK ? 0 : -1;
    if (cipher == SS_CIPHER_AES_128_GCM)
        return rc_aes128gcm_open(key, nonce, NULL, 0, ct, ct_len, tag, pt) == RC_OK ? 0 : -1;
    if (cipher == SS_CIPHER_CHACHA20_POLY1305)
        return rc_chacha20poly1305_open(key, nonce, NULL, 0, ct, ct_len, tag, pt) == RC_OK ? 0 : -1;
    return -1;
}

void ss_client_init(shadowsocks_client_t *c, const char *method,
                    const char *password, const vless_dest_t *dest) {
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->cipher = ss_cipher_from_name(method);
    if (c->cipher == SS_CIPHER_AES_128_GCM) {
        c->key_len = 16;
        c->salt_len = 16;
    } else {
        c->key_len = 32;
        c->salt_len = 32;
    }
    if (password && password[0]) {
        ss_derive_master_key(password, strlen(password), c->master_key, c->key_len);
    }
    if (dest) c->dest = *dest;
}

int ss_client_encrypt_chunk(shadowsocks_client_t *c,
                            const uint8_t *plain, size_t plain_len,
                            uint8_t *out, size_t cap, size_t *out_len) {
    if (!c || !plain || !out || !out_len) return SS_ERR_ARG;
    if (plain_len > 0x3fff) return SS_ERR_ARG;
    if (2 + 16 + plain_len + 16 > cap) return SS_ERR_ARG;

    size_t off = 0;
    uint8_t len_be[2];
    len_be[0] = (uint8_t)(plain_len >> 8);
    len_be[1] = (uint8_t)(plain_len & 0xff);

    /* 1. Seal length */
    if (ss_aead_seal(c->cipher, c->enc_subkey, c->enc_nonce,
                     len_be, 2, out + off, out + off + 2) != 0)
        return SS_ERR_CRYPTO;
    ss_inc_nonce(c->enc_nonce);
    off += 18;

    /* 2. Seal payload */
    if (ss_aead_seal(c->cipher, c->enc_subkey, c->enc_nonce,
                     plain, plain_len, out + off, out + off + plain_len) != 0)
        return SS_ERR_CRYPTO;
    ss_inc_nonce(c->enc_nonce);
    off += plain_len + 16;

    *out_len = off;
    return SS_OK;
}

int ss_client_build_request(shadowsocks_client_t *c,
                            const uint8_t *payload, size_t payload_len,
                            uint8_t *out, size_t cap, size_t *out_len) {
    if (!c || !out || !out_len) return SS_ERR_ARG;
    if (c->cipher == SS_CIPHER_UNKNOWN) return SS_ERR_CIPHER;

    /* Generate client salt */
    if (RAND_bytes(c->enc_salt, (int)c->salt_len) != 1) return SS_ERR_CRYPTO;
    static const uint8_t info[] = "ss-subkey";
    if (ss_hkdf_sha1(c->master_key, c->key_len, c->enc_salt, c->salt_len,
                     info, sizeof info - 1, c->enc_subkey, c->key_len) != 0)
        return SS_ERR_CRYPTO;
    memset(c->enc_nonce, 0, sizeof c->enc_nonce);

    if (c->salt_len > cap) return SS_ERR_ARG;
    memcpy(out, c->enc_salt, c->salt_len);
    size_t total_len = c->salt_len;

    /* Format target address payload */
    uint8_t target[512];
    size_t tlen = 0;
    uint8_t atyp = 0;
    if (c->dest.atyp == VLESS_ADDR_IPV4) atyp = 0x01;
    else if (c->dest.atyp == VLESS_ADDR_DOMAIN) atyp = 0x03;
    else if (c->dest.atyp == VLESS_ADDR_IPV6) atyp = 0x04;
    else return SS_ERR_ARG;

    target[tlen++] = atyp;
    if (atyp == 0x01) {
        memcpy(target + tlen, c->dest.host_addr, 4);
        tlen += 4;
    } else if (atyp == 0x03) {
        size_t dl = strlen(c->dest.domain);
        if (dl > 255) return SS_ERR_ARG;
        target[tlen++] = (uint8_t)dl;
        memcpy(target + tlen, c->dest.domain, dl);
        tlen += dl;
    } else if (atyp == 0x04) {
        memcpy(target + tlen, c->dest.host_addr, 16);
        tlen += 16;
    }
    target[tlen++] = (uint8_t)(c->dest.port >> 8);
    target[tlen++] = (uint8_t)(c->dest.port & 0xff);

    /* Append initial payload */
    uint8_t chunk_buf[16384];
    size_t chunk_len = tlen + payload_len;
    /* the AEAD chunk length prefix is 14 bits (ss_client_encrypt_chunk caps
       plain_len at 0x3fff); bounding against the buffer's own 16384 bytes
       instead let a 16384-byte chunk pass here only to be rejected as a
       crypto error one call later */
    if (chunk_len > 0x3fff) return SS_ERR_ARG;
    memcpy(chunk_buf, target, tlen);
    if (payload && payload_len > 0)
        memcpy(chunk_buf + tlen, payload, payload_len);

    size_t enc_len = 0;
    if (ss_client_encrypt_chunk(c, chunk_buf, chunk_len, out + total_len, cap - total_len, &enc_len) != SS_OK)
        return SS_ERR_CRYPTO;
    total_len += enc_len;

    c->enc_salt_sent = 1;
    c->handshake_done = 1;
    *out_len = total_len;
    return SS_OK;
}

int ss_client_feed_downstream(shadowsocks_client_t *c,
                              const uint8_t *in, size_t in_len,
                              uint8_t *plain_out, size_t plain_cap,
                              size_t *plain_len) {
    if (!c || !plain_out || !plain_len) return SS_ERR_ARG;
    *plain_len = 0;
    if (in && in_len > 0) {
        if (c->dec_stage_len + in_len > sizeof c->dec_stage) return SS_ERR_ARG;
        memcpy(c->dec_stage + c->dec_stage_len, in, in_len);
        c->dec_stage_len += in_len;
    }

    /* 1. Expect server salt */
    if (!c->dec_salt_received) {
        if (c->dec_stage_len < c->salt_len) return SS_NEED_MORE;
        memcpy(c->dec_salt, c->dec_stage, c->salt_len);
        static const uint8_t info[] = "ss-subkey";
        if (ss_hkdf_sha1(c->master_key, c->key_len, c->dec_salt, c->salt_len,
                         info, sizeof info - 1, c->dec_subkey, c->key_len) != 0)
            return SS_ERR_CRYPTO;
        memset(c->dec_nonce, 0, sizeof c->dec_nonce);
        c->dec_salt_received = 1;
        memmove(c->dec_stage, c->dec_stage + c->salt_len, c->dec_stage_len - c->salt_len);
        c->dec_stage_len -= c->salt_len;
    }

    /* 2. Unpack available AEAD chunks */
    size_t plain_total = 0;
    while (c->dec_stage_len >= 18) {
        uint8_t len_be[2];
        if (ss_aead_open(c->cipher, c->dec_subkey, c->dec_nonce,
                         c->dec_stage, 2, c->dec_stage + 2, len_be) != 0)
            return SS_ERR_BAD_TAG;
        size_t chunk_len = ((size_t)len_be[0] << 8) | (size_t)len_be[1];
        if (chunk_len > 0x3fff) return SS_ERR_CRYPTO;

        size_t total_chunk = 18 + chunk_len + 16;
        if (c->dec_stage_len < total_chunk) {
            /* Need more bytes for the payload */
            break;
        }
        if (plain_total + chunk_len > plain_cap) {
/* caller's buffer is full; leave this chunk staged for the next call so the
   wire nonce, which only advances once a chunk is actually consumed, stays
   in sync with what the server encrypted it with */
            break;
        }
        ss_inc_nonce(c->dec_nonce);

        if (ss_aead_open(c->cipher, c->dec_subkey, c->dec_nonce,
                         c->dec_stage + 18, chunk_len,
                         c->dec_stage + 18 + chunk_len, plain_out + plain_total) != 0)
            return SS_ERR_BAD_TAG;
        ss_inc_nonce(c->dec_nonce);
        plain_total += chunk_len;

        memmove(c->dec_stage, c->dec_stage + total_chunk, c->dec_stage_len - total_chunk);
        c->dec_stage_len -= total_chunk;
    }

    *plain_len = plain_total;
    return SS_OK;
}
