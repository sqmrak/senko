/* vmess with the aead header (xray proxy/vmess/aead and encoding/client.go):
   chunk stream, chunk masking and global padding, the options xray's own
   client sets for aes-128-gcm and chacha20-poly1305 */
#include "vmess_client.h"

#include <string.h>
#include <time.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "third_party/mlkem768/fips202.h"

#define VMESS_VERSION 0x01
#define VMESS_CMD_TCP 0x01
#define VMESS_CMD_UDP 0x02
#define VMESS_OPT_CHUNK_STREAM 0x01
#define VMESS_OPT_CHUNK_MASKING 0x04
#define VMESS_OPT_GLOBAL_PADDING 0x08
#define VMESS_TAG 16
/* the response header is 4 bytes plus one command of at most 255 */
#define VMESS_RESP_HEADER_MAX (4 + 255)
/* key plus one 64 byte pad per kdf level; the deepest path has four levels */
#define VMESS_KDF_MSG_MAX (32 + 64 * 5)

static const char k_kdf_salt[] = "VMess AEAD KDF";
static const char k_auth_id_key[] = "AES Auth ID Encryption";
static const char k_header_len_key[] = "VMess Header AEAD Key_Length";
static const char k_header_len_iv[] = "VMess Header AEAD Nonce_Length";
static const char k_header_key[] = "VMess Header AEAD Key";
static const char k_header_iv[] = "VMess Header AEAD Nonce";
static const char k_resp_len_key[] = "AEAD Resp Header Len Key";
static const char k_resp_len_iv[] = "AEAD Resp Header Len IV";
static const char k_resp_key[] = "AEAD Resp Header Key";
static const char k_resp_iv[] = "AEAD Resp Header IV";

static int sha256(const uint8_t *msg, size_t len, uint8_t out[32]) {
    unsigned int n = 0;
    return EVP_Digest(msg, len, out, &n, EVP_sha256(), NULL) == 1 && n == 32 ? 0 : -1;
}

/* level 0 is sha256; level k is hmac keyed with keys[k - 1] over level k - 1,
   which is how xray stacks hmac.New(parent, path element) */
static int kdf_level(const uint8_t *const *keys, const size_t *key_len, size_t level,
                     const uint8_t *msg, size_t len, uint8_t out[32]) {
    if (level == 0) return sha256(msg, len, out);
    if (len > VMESS_KDF_MSG_MAX - 64) return -1;
    uint8_t key[64];
    memset(key, 0, sizeof key);
    if (key_len[level - 1] > sizeof key) {
        if (kdf_level(keys, key_len, level - 1, keys[level - 1], key_len[level - 1], key) != 0)
            return -1;
    } else {
        memcpy(key, keys[level - 1], key_len[level - 1]);
    }
    uint8_t buf[VMESS_KDF_MSG_MAX];
    uint8_t inner[32];
    for (size_t i = 0; i < 64; ++i) buf[i] = key[i] ^ 0x36;
    if (len) memcpy(buf + 64, msg, len);
    if (kdf_level(keys, key_len, level - 1, buf, 64 + len, inner) != 0) return -1;
    for (size_t i = 0; i < 64; ++i) buf[i] = key[i] ^ 0x5c;
    memcpy(buf + 64, inner, sizeof inner);
    return kdf_level(keys, key_len, level - 1, buf, 64 + sizeof inner, out);
}

int vmess_kdf(const uint8_t *key, size_t key_len,
              const uint8_t *const *path, const size_t *path_len, size_t path_count,
              uint8_t out[32]) {
    if (!key || !out || path_count > 3 || (path_count && (!path || !path_len))) return -1;
    const uint8_t *keys[4];
    size_t lens[4];
    keys[0] = (const uint8_t *)k_kdf_salt;
    lens[0] = sizeof k_kdf_salt - 1;
    for (size_t i = 0; i < path_count; ++i) {
        keys[i + 1] = path[i];
        lens[i + 1] = path_len[i];
    }
    return kdf_level(keys, lens, path_count + 1, key, key_len, out);
}

/* KDF over a label and up to two binary path elements */
static int kdf3(const uint8_t *key, size_t key_len, const char *label,
                const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len,
                uint8_t out[32]) {
    const uint8_t *path[3] = { (const uint8_t *)label, a, b };
    size_t lens[3] = { strlen(label), a_len, b_len };
    size_t count = b ? 3 : (a ? 2 : 1);
    return vmess_kdf(key, key_len, path, lens, count, out);
}

static int aead_seal(vmess_cipher_t cipher, const uint8_t *key, const uint8_t nonce[12],
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *plain, size_t len, uint8_t *out) {
    const EVP_CIPHER *kind = cipher == VMESS_CIPHER_CHACHA20_POLY1305
        ? EVP_chacha20_poly1305() : EVP_aes_128_gcm();
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int ok = 0, n = 0;
    if (!ctx) return -1;
    if (EVP_EncryptInit_ex(ctx, kind, NULL, NULL, NULL) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, NULL) == 1 &&
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) == 1 &&
        (aad_len == 0 || EVP_EncryptUpdate(ctx, NULL, &n, aad, (int)aad_len) == 1) &&
        (len == 0 || EVP_EncryptUpdate(ctx, out, &n, plain, (int)len) == 1) &&
        EVP_EncryptFinal_ex(ctx, out + len, &n) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, VMESS_TAG, out + len) == 1)
        ok = 1;
    EVP_CIPHER_CTX_free(ctx);
    return ok ? 0 : -1;
}

/* 0, VMESS_ERR_BAD_TAG when the tag does not match, VMESS_ERR_CRYPTO otherwise */
static int aead_open(vmess_cipher_t cipher, const uint8_t *key, const uint8_t nonce[12],
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *sealed, size_t len, uint8_t *out) {
    if (len < VMESS_TAG) return VMESS_ERR_PROTO;
    size_t plain = len - VMESS_TAG;
    const EVP_CIPHER *kind = cipher == VMESS_CIPHER_CHACHA20_POLY1305
        ? EVP_chacha20_poly1305() : EVP_aes_128_gcm();
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int n = 0, rc = VMESS_ERR_CRYPTO;
    uint8_t tag[VMESS_TAG];
    if (!ctx) return VMESS_ERR_CRYPTO;
    memcpy(tag, sealed + plain, VMESS_TAG);
    if (EVP_DecryptInit_ex(ctx, kind, NULL, NULL, NULL) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, 12, NULL) == 1 &&
        EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) == 1 &&
        (aad_len == 0 || EVP_DecryptUpdate(ctx, NULL, &n, aad, (int)aad_len) == 1) &&
        (plain == 0 || EVP_DecryptUpdate(ctx, out, &n, sealed, (int)plain) == 1) &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, VMESS_TAG, tag) == 1)
        rc = EVP_DecryptFinal_ex(ctx, out + plain, &n) == 1 ? VMESS_OK : VMESS_ERR_BAD_TAG;
    EVP_CIPHER_CTX_free(ctx);
    return rc;
}

static int aes_ecb_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int n = 0, ok = 0;
    if (!ctx) return -1;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL) == 1 &&
        EVP_CIPHER_CTX_set_padding(ctx, 0) == 1 &&
        EVP_EncryptUpdate(ctx, out, &n, in, 16) == 1 && n == 16)
        ok = 1;
    EVP_CIPHER_CTX_free(ctx);
    return ok ? 0 : -1;
}

static int md5(const uint8_t *in, size_t len, uint8_t out[16]) {
    unsigned int n = 0;
    return EVP_Digest(in, len, out, &n, EVP_md5(), NULL) == 1 && n == 16 ? 0 : -1;
}

/* xray stretches the 16 byte body key for chacha: md5(key) || md5(md5(key)) */
static int body_key(vmess_cipher_t cipher, const uint8_t key[16], uint8_t out[32]) {
    if (cipher != VMESS_CIPHER_CHACHA20_POLY1305) {
        memcpy(out, key, 16);
        return 0;
    }
    if (md5(key, 16, out) != 0) return -1;
    return md5(out, 16, out + 16);
}

static void mask_init(vmess_shake_t *m, const uint8_t iv[16]) {
    shake128ctx st;
    /* the vendored sponge allocates its state; it is copied out and freed so
       a session needs no teardown hook for it */
    shake128_absorb(&st, iv, 16);
    memcpy(m->s, st.ctx, sizeof m->s);
    shake128_ctx_release(&st);
    m->pos = sizeof m->block;
}

static uint16_t mask_next(vmess_shake_t *m) {
    if (m->pos + 2 > sizeof m->block) {
        shake128ctx st;
        st.ctx = m->s;
        shake128_squeezeblocks(m->block, 1, &st);
        m->pos = 0;
    }
    uint16_t v = (uint16_t)((m->block[m->pos] << 8) | m->block[m->pos + 1]);
    m->pos += 2;
    return v;
}

static void chunk_nonce(const uint8_t iv[16], uint16_t count, uint8_t nonce[12]) {
    memcpy(nonce, iv, 12);
    nonce[0] = (uint8_t)(count >> 8);
    nonce[1] = (uint8_t)count;
}

static uint32_t fnv1a32(const uint8_t *p, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t crc32_ieee(const uint8_t *p, size_t len) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static int default_random(uint8_t *out, size_t len) {
    return RAND_bytes(out, (int)len) == 1 ? 0 : -1;
}

vmess_cipher_t vmess_cipher_from_name(const char *name) {
    if (name && strcmp(name, "aes-128-gcm") == 0) return VMESS_CIPHER_AES_128_GCM;
    if (name && strcmp(name, "chacha20-poly1305") == 0) return VMESS_CIPHER_CHACHA20_POLY1305;
#if defined(__aarch64__) || defined(__x86_64__)
    return VMESS_CIPHER_AES_128_GCM;
#else
    /* armv7 iphones have no aes instructions */
    return VMESS_CIPHER_CHACHA20_POLY1305;
#endif
}

int vmess_client_init(vmess_client_t *c, const uint8_t uuid[VLESS_UUID_LEN],
                      const char *security, const vless_dest_t *dest,
                      vmess_random_fn random) {
    if (!c || !uuid) return VMESS_ERR_ARG;
    memset(c, 0, sizeof *c);
    c->random = random ? random : default_random;
    c->cipher = vmess_cipher_from_name(security);
    if (dest) c->dest = *dest;

    /* cmd key: md5(uuid || the fixed vmess salt), common/protocol/id.go */
    static const char salt[] = "c48619fe-8f02-49e0-b9e9-edf763e17e21";
    uint8_t seed[VLESS_UUID_LEN + sizeof salt - 1];
    memcpy(seed, uuid, VLESS_UUID_LEN);
    memcpy(seed + VLESS_UUID_LEN, salt, sizeof salt - 1);
    if (md5(seed, sizeof seed, c->cmd_key) != 0) return VMESS_ERR_CRYPTO;

    uint8_t r[33];
    if (c->random(r, sizeof r) != 0) return VMESS_ERR_CRYPTO;
    memcpy(c->req_key, r, 16);
    memcpy(c->req_iv, r + 16, 16);
    c->resp_v = r[32];
    uint8_t d[32];
    if (sha256(c->req_key, 16, d) != 0) return VMESS_ERR_CRYPTO;
    memcpy(c->resp_key, d, 16);
    if (sha256(c->req_iv, 16, d) != 0) return VMESS_ERR_CRYPTO;
    memcpy(c->resp_iv, d, 16);
    if (body_key(c->cipher, c->req_key, c->req_body_key) != 0 ||
        body_key(c->cipher, c->resp_key, c->resp_body_key) != 0)
        return VMESS_ERR_CRYPTO;
    mask_init(&c->enc_mask, c->req_iv);
    mask_init(&c->dec_mask, c->resp_iv);
    return VMESS_OK;
}

static int seal_header(vmess_client_t *c, const uint8_t *plain, size_t len,
                       uint8_t *out, size_t cap, size_t *out_len) {
    size_t total = 16 + 18 + 8 + len + VMESS_TAG;
    if (total > cap) return VMESS_ERR_ARG;

    int64_t now = c->unix_time ? c->unix_time : (int64_t)time(NULL);
    uint8_t id[16];
    for (int i = 0; i < 8; ++i) id[i] = (uint8_t)((uint64_t)now >> (56 - 8 * i));
    if (c->random(id + 8, 4) != 0) return VMESS_ERR_CRYPTO;
    uint32_t crc = crc32_ieee(id, 12);
    id[12] = (uint8_t)(crc >> 24);
    id[13] = (uint8_t)(crc >> 16);
    id[14] = (uint8_t)(crc >> 8);
    id[15] = (uint8_t)crc;
    uint8_t k[32];
    if (kdf3(c->cmd_key, 16, k_auth_id_key, NULL, 0, NULL, 0, k) != 0 ||
        aes_ecb_block(k, id, out) != 0)
        return VMESS_ERR_CRYPTO;
    const uint8_t *auth_id = out;

    uint8_t nonce8[8];
    if (c->random(nonce8, sizeof nonce8) != 0) return VMESS_ERR_CRYPTO;
    uint8_t len_be[2] = { (uint8_t)(len >> 8), (uint8_t)len };
    uint8_t key[32], iv[32];
    if (kdf3(c->cmd_key, 16, k_header_len_key, auth_id, 16, nonce8, 8, key) != 0 ||
        kdf3(c->cmd_key, 16, k_header_len_iv, auth_id, 16, nonce8, 8, iv) != 0 ||
        aead_seal(VMESS_CIPHER_AES_128_GCM, key, iv, auth_id, 16, len_be, 2, out + 16) != 0)
        return VMESS_ERR_CRYPTO;
    memcpy(out + 16 + 18, nonce8, 8);
    if (kdf3(c->cmd_key, 16, k_header_key, auth_id, 16, nonce8, 8, key) != 0 ||
        kdf3(c->cmd_key, 16, k_header_iv, auth_id, 16, nonce8, 8, iv) != 0 ||
        aead_seal(VMESS_CIPHER_AES_128_GCM, key, iv, auth_id, 16, plain, len,
                  out + 16 + 18 + 8) != 0)
        return VMESS_ERR_CRYPTO;
    *out_len = total;
    return VMESS_OK;
}

int vmess_client_encrypt_chunk(vmess_client_t *c,
                               const uint8_t *plain, size_t plain_len,
                               uint8_t *out, size_t cap, size_t *out_len) {
    if (!c || !out || !out_len || (!plain && plain_len)) return VMESS_ERR_ARG;
    if (plain_len > VMESS_CHUNK_PAYLOAD_MAX) return VMESS_ERR_ARG;
    size_t sealed = plain_len + VMESS_TAG;
    /* checked against the largest padding: a mask draw cannot be put back */
    if (2 + sealed + 63 > cap) return VMESS_ERR_ARG;
    uint16_t padding = (uint16_t)(mask_next(&c->enc_mask) % 64);
    size_t total = 2 + sealed + padding;
    uint16_t size = (uint16_t)((sealed + padding) ^ mask_next(&c->enc_mask));
    out[0] = (uint8_t)(size >> 8);
    out[1] = (uint8_t)size;
    uint8_t nonce[12];
    chunk_nonce(c->req_iv, c->enc_count++, nonce);
    if (aead_seal(c->cipher, c->req_body_key, nonce, NULL, 0, plain, plain_len, out + 2) != 0)
        return VMESS_ERR_CRYPTO;
    if (padding && c->random(out + 2 + sealed, padding) != 0) return VMESS_ERR_CRYPTO;
    *out_len = total;
    return VMESS_OK;
}

int vmess_client_build_request(vmess_client_t *c,
                               const uint8_t *payload, size_t payload_len,
                               uint8_t *out, size_t cap, size_t *out_len) {
    if (!c || !out || !out_len || (!payload && payload_len)) return VMESS_ERR_ARG;
    if (c->header_sent) return VMESS_ERR_ARG;

    uint8_t h[41 + 256 + 15 + 4];
    size_t n = 0;
    h[n++] = VMESS_VERSION;
    memcpy(h + n, c->req_iv, 16); n += 16;
    memcpy(h + n, c->req_key, 16); n += 16;
    h[n++] = c->resp_v;
    h[n++] = VMESS_OPT_CHUNK_STREAM | VMESS_OPT_CHUNK_MASKING | VMESS_OPT_GLOBAL_PADDING;
    uint8_t pad_len = 0;
    if (c->random(&pad_len, 1) != 0) return VMESS_ERR_CRYPTO;
    pad_len &= 0x0F;
    h[n++] = (uint8_t)((pad_len << 4) | c->cipher);
    h[n++] = 0;
    h[n++] = c->packet ? VMESS_CMD_UDP : VMESS_CMD_TCP;
    /* vmess writes the port before the address, unlike vless */
    h[n++] = (uint8_t)(c->dest.port >> 8);
    h[n++] = (uint8_t)c->dest.port;
    if (c->dest.atyp == VLESS_ADDR_IPV4) {
        h[n++] = 0x01;
        memcpy(h + n, c->dest.host_addr, 4); n += 4;
    } else if (c->dest.atyp == VLESS_ADDR_DOMAIN) {
        size_t dl = strlen(c->dest.domain);
        if (dl == 0 || dl > 255) return VMESS_ERR_ARG;
        h[n++] = 0x02;
        h[n++] = (uint8_t)dl;
        memcpy(h + n, c->dest.domain, dl); n += dl;
    } else if (c->dest.atyp == VLESS_ADDR_IPV6) {
        h[n++] = 0x03;
        memcpy(h + n, c->dest.host_addr, 16); n += 16;
    } else {
        return VMESS_ERR_ARG;
    }
    if (pad_len && c->random(h + n, pad_len) != 0) return VMESS_ERR_CRYPTO;
    n += pad_len;
    uint32_t f = fnv1a32(h, n);
    h[n++] = (uint8_t)(f >> 24);
    h[n++] = (uint8_t)(f >> 16);
    h[n++] = (uint8_t)(f >> 8);
    h[n++] = (uint8_t)f;

    size_t off = 0;
    int rc = seal_header(c, h, n, out, cap, &off);
    memset(h, 0, sizeof h);
    if (rc != VMESS_OK) return rc;
    c->header_sent = 1;
    while (payload_len > 0) {
        size_t take = payload_len < VMESS_CHUNK_PAYLOAD_MAX ? payload_len : VMESS_CHUNK_PAYLOAD_MAX;
        size_t w = 0;
        rc = vmess_client_encrypt_chunk(c, payload, take, out + off, cap - off, &w);
        if (rc != VMESS_OK) return rc;
        off += w;
        payload += take;
        payload_len -= take;
    }
    *out_len = off;
    return VMESS_OK;
}

static void consume(vmess_client_t *c, size_t n) {
    memmove(c->dec_stage, c->dec_stage + n, c->dec_stage_len - n);
    c->dec_stage_len -= n;
}

static int open_response_header(vmess_client_t *c) {
    uint8_t key[32], iv[32];
    if (!c->resp_header_len) {
        if (c->dec_stage_len < 18) return VMESS_NEED_MORE;
        uint8_t len_be[2];
        if (kdf3(c->resp_key, 16, k_resp_len_key, NULL, 0, NULL, 0, key) != 0 ||
            kdf3(c->resp_iv, 16, k_resp_len_iv, NULL, 0, NULL, 0, iv) != 0)
            return VMESS_ERR_CRYPTO;
        int rc = aead_open(VMESS_CIPHER_AES_128_GCM, key, iv, NULL, 0, c->dec_stage, 18, len_be);
        if (rc != VMESS_OK) return rc;
        size_t len = ((size_t)len_be[0] << 8) | len_be[1];
        if (len < 4 || len > VMESS_RESP_HEADER_MAX) return VMESS_ERR_PROTO;
        c->resp_header_len = len;
        consume(c, 18);
    }
    if (c->dec_stage_len < c->resp_header_len + VMESS_TAG) return VMESS_NEED_MORE;
    uint8_t h[VMESS_RESP_HEADER_MAX];
    if (kdf3(c->resp_key, 16, k_resp_key, NULL, 0, NULL, 0, key) != 0 ||
        kdf3(c->resp_iv, 16, k_resp_iv, NULL, 0, NULL, 0, iv) != 0)
        return VMESS_ERR_CRYPTO;
    int rc = aead_open(VMESS_CIPHER_AES_128_GCM, key, iv, NULL, 0, c->dec_stage,
                       c->resp_header_len + VMESS_TAG, h);
    if (rc != VMESS_OK) return rc;
    /* a response for another request, or a command longer than the header */
    if (h[0] != c->resp_v) return VMESS_ERR_PROTO;
    if (h[2] != 0 && c->resp_header_len < 4u + h[3]) return VMESS_ERR_PROTO;
    consume(c, c->resp_header_len + VMESS_TAG);
    c->resp_header_done = 1;
    return VMESS_OK;
}

int vmess_client_feed_downstream(vmess_client_t *c,
                                 const uint8_t *in, size_t in_len,
                                 uint8_t *plain_out, size_t plain_cap,
                                 size_t *plain_len) {
    if (!c || !plain_out || !plain_len || (!in && in_len)) return VMESS_ERR_ARG;
    *plain_len = 0;
    if (in_len > 0) {
        if (c->dec_stage_len + in_len > sizeof c->dec_stage) return VMESS_ERR_ARG;
        memcpy(c->dec_stage + c->dec_stage_len, in, in_len);
        c->dec_stage_len += in_len;
    }
    if (c->eof) return VMESS_EOF;
    if (!c->resp_header_done) {
        int rc = open_response_header(c);
        if (rc == VMESS_NEED_MORE) return VMESS_NEED_MORE;
        if (rc != VMESS_OK) return rc;
    }

    size_t total = 0;
    for (;;) {
        if (!c->have_size) {
            if (c->dec_stage_len < 2) break;
            /* xray draws the padding length before the size mask */
            c->chunk_padding = (uint16_t)(mask_next(&c->dec_mask) % 64);
            c->chunk_size = (uint16_t)(((c->dec_stage[0] << 8) | c->dec_stage[1]) ^
                                       mask_next(&c->dec_mask));
            c->have_size = 1;
            consume(c, 2);
        }
        size_t size = c->chunk_size, padding = c->chunk_padding;
        if (size == VMESS_TAG + padding) {
            c->eof = 1;
            c->have_size = 0;
            *plain_len = total;
            return VMESS_EOF;
        }
        if (size < VMESS_TAG + padding || size > VMESS_CHUNK_WIRE_MAX) return VMESS_ERR_PROTO;
        if (c->dec_stage_len < size) break;
        size_t plain = size - padding - VMESS_TAG;
        size_t prefix = c->packet ? 2 : 0;
        /* a datagram longer than the 16 bit frame cannot have been sent */
        if (c->packet && plain > 0xFFFF) return VMESS_ERR_PROTO;
        /* a full output buffer leaves the chunk staged so the nonce counter only
           advances for chunks actually opened */
        if (total + prefix + plain > plain_cap) break;
        uint8_t nonce[12];
        chunk_nonce(c->resp_iv, c->dec_count, nonce);
        int rc = aead_open(c->cipher, c->resp_body_key, nonce, NULL, 0, c->dec_stage,
                           size - padding, plain_out + total + prefix);
        if (rc != VMESS_OK) return rc;
        c->dec_count++;
        if (prefix) {
            plain_out[total] = (uint8_t)(plain >> 8);
            plain_out[total + 1] = (uint8_t)plain;
        }
        total += prefix + plain;
        c->have_size = 0;
        consume(c, size);
    }
    *plain_len = total;
    return total ? VMESS_OK : VMESS_NEED_MORE;
}
