#define _DEFAULT_SOURCE

#include "awg_handshake.h"

#include "reality_crypto.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define AWG_HASH_LEN 32
#define AWG_TAG_LEN 16

static const uint8_t k_noise_name[] = "Noise_IKpsk2_25519_ChaChaPoly_BLAKE2s";
static const uint8_t k_wireguard_identifier[] = "WireGuard v1 zx2c4 Jason@zx2c4.com";
static const uint8_t k_mac1_label[] = "mac1----";

static void write_le32(uint8_t out[4], uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static uint32_t read_le32(const uint8_t in[4]) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static int blake2s(const uint8_t *input, size_t input_len, uint8_t out[AWG_HASH_LEN]) {
    unsigned int out_len = 0;
    return EVP_Digest(input, input_len, out, &out_len, EVP_blake2s256(), NULL) == 1 &&
           out_len == AWG_HASH_LEN ? 0 : -1;
}

static int blake2s_many(const uint8_t hash[AWG_HASH_LEN], const uint8_t *input,
                        size_t input_len, uint8_t out[AWG_HASH_LEN]) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned int out_len = 0;
    int ok = ctx && EVP_DigestInit_ex(ctx, EVP_blake2s256(), NULL) == 1 &&
             EVP_DigestUpdate(ctx, hash, AWG_HASH_LEN) == 1 &&
             (!input_len || EVP_DigestUpdate(ctx, input, input_len) == 1) &&
             EVP_DigestFinal_ex(ctx, out, &out_len) == 1 && out_len == AWG_HASH_LEN;
    EVP_MD_CTX_free(ctx);
    return ok ? 0 : -1;
}

static int hmac_blake2s(const uint8_t *key, size_t key_len,
                         const uint8_t *input, size_t input_len,
                         uint8_t out[AWG_HASH_LEN]) {
    unsigned int out_len = 0;
    return HMAC(EVP_blake2s256(), key, (int)key_len, input, input_len, out, &out_len) &&
           out_len == AWG_HASH_LEN ? 0 : -1;
}

static uint32_t b2r(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
static uint32_t b2l(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void b2s_keyed_block(uint32_t h[8], const uint8_t b[64], uint64_t n, int last) {
    static const uint32_t iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    static const uint8_t s[10][16]={{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},{14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3},{11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4},{7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8},{9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13},{2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9},{12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11},{13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10},{6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5},{10,2,8,4,7,6,1,5,15,11,9,14,3,12,13,0}};
    uint32_t v[16],m[16]; for(int i=0;i<8;i++){v[i]=h[i];v[i+8]=iv[i];} v[12]^=(uint32_t)n;v[13]^=(uint32_t)(n>>32);if(last)v[14]=~v[14];for(int i=0;i<16;i++)m[i]=b2l(b+4*i);
#define G(a,b,c,d,x,y) do{v[a]+=v[b]+m[x];v[d]=b2r(v[d]^v[a],16);v[c]+=v[d];v[b]=b2r(v[b]^v[c],12);v[a]+=v[b]+m[y];v[d]=b2r(v[d]^v[a],8);v[c]+=v[d];v[b]=b2r(v[b]^v[c],7);}while(0)
    for(int r=0;r<10;r++){const uint8_t *q=s[r];G(0,4,8,12,q[0],q[1]);G(1,5,9,13,q[2],q[3]);G(2,6,10,14,q[4],q[5]);G(3,7,11,15,q[6],q[7]);G(0,5,10,15,q[8],q[9]);G(1,6,11,12,q[10],q[11]);G(2,7,8,13,q[12],q[13]);G(3,4,9,14,q[14],q[15]);}
#undef G
    for(int i=0;i<8;i++)h[i]^=v[i]^v[i+8];
}
static int blake2s_keyed_len(const uint8_t *key,size_t kl,const uint8_t *in,size_t il,uint8_t *out,size_t ol) { if(kl>32||ol==0||ol>32)return -1; uint32_t h[8]={0x6a09e667^(0x01010000U|((uint32_t)kl<<8)|(uint32_t)ol),0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};uint8_t b[64]={0};uint8_t full[32];uint64_t n=0;memcpy(b,key,kl);n=64;b2s_keyed_block(h,b,n,il==0);while(il>64){b2s_keyed_block(h,in,n+=64,0);in+=64;il-=64;}memset(b,0,64);memcpy(b,in,il);b2s_keyed_block(h,b,n+il,1);for(int i=0;i<8;i++){full[4*i]=h[i];full[4*i+1]=h[i]>>8;full[4*i+2]=h[i]>>16;full[4*i+3]=h[i]>>24;}memcpy(out,full,ol);return 0; }
int awg_blake2s_keyed(const uint8_t *key,size_t kl,const uint8_t *in,size_t il,uint8_t out[32]) { return blake2s_keyed_len(key,kl,in,il,out,32); }
int awg_blake2s_keyed_16(const uint8_t *key,size_t kl,const uint8_t *in,size_t il,uint8_t out[16]) { return blake2s_keyed_len(key,kl,in,il,out,16); }

static int kdf2(const uint8_t chain_key[32], const uint8_t *input, size_t input_len,
                uint8_t out_chain_key[32], uint8_t out_key[32]) {
    uint8_t temp[32];
    uint8_t part[33];
    int ok = hmac_blake2s(chain_key, 32, input, input_len, temp) == 0;
    if (ok) {
        part[0] = 1;
        ok = hmac_blake2s(temp, 32, part, 1, out_chain_key) == 0;
    }
    if (ok) {
        memcpy(part, out_chain_key, 32);
        part[32] = 2;
        ok = hmac_blake2s(temp, 32, part, sizeof part, out_key) == 0;
    }
    OPENSSL_cleanse(temp, sizeof temp);
    OPENSSL_cleanse(part, sizeof part);
    return ok ? 0 : -1;
}

static int kdf3(const uint8_t chain_key[32], const uint8_t *input, size_t input_len,
                uint8_t out_chain_key[32], uint8_t out_hash_key[32], uint8_t out_key[32]) {
    uint8_t temp[32];
    uint8_t part[33];
    int ok = hmac_blake2s(chain_key, 32, input, input_len, temp) == 0;
    if (ok) {
        part[0] = 1;
        ok = hmac_blake2s(temp, 32, part, 1, out_chain_key) == 0;
    }
    if (ok) {
        memcpy(part, out_chain_key, 32);
        part[32] = 2;
        ok = hmac_blake2s(temp, 32, part, sizeof part, out_hash_key) == 0;
    }
    if (ok) {
        memcpy(part, out_hash_key, 32);
        part[32] = 3;
        ok = hmac_blake2s(temp, 32, part, sizeof part, out_key) == 0;
    }
    OPENSSL_cleanse(temp, sizeof temp);
    OPENSSL_cleanse(part, sizeof part);
    return ok ? 0 : -1;
}

static int mix_hash(uint8_t hash[32], const uint8_t *input, size_t input_len) {
    uint8_t next[32];
    if (blake2s_many(hash, input, input_len, next) != 0) return -1;
    memcpy(hash, next, sizeof next);
    OPENSSL_cleanse(next, sizeof next);
    return 0;
}

static int mix_key(uint8_t chain_key[32], const uint8_t *input, size_t input_len,
                   uint8_t key[32]) {
    uint8_t next_chain_key[32];
    if (kdf2(chain_key, input, input_len, next_chain_key, key) != 0) return -1;
    memcpy(chain_key, next_chain_key, sizeof next_chain_key);
    OPENSSL_cleanse(next_chain_key, sizeof next_chain_key);
    return 0;
}

static void handshake_nonce(uint64_t counter, uint8_t nonce[12]) {
    memset(nonce, 0, 12);
    for (size_t i = 0; i < 8; ++i)
        nonce[4 + i] = (uint8_t)(counter >> (8 * i));
}

static int seal_and_hash(uint8_t hash[32], const uint8_t key[32], uint64_t counter,
                         const uint8_t *plain, size_t plain_len, uint8_t *out) {
    uint8_t nonce[12];
    uint8_t tag[AWG_TAG_LEN];
    handshake_nonce(counter, nonce);
    int ok = rc_chacha20poly1305_seal(key, nonce, hash, 32, plain, plain_len, out, tag) == RC_OK;
    if (ok) {
        memcpy(out + plain_len, tag, sizeof tag);
        ok = mix_hash(hash, out, plain_len + sizeof tag) == 0;
    }
    OPENSSL_cleanse(nonce, sizeof nonce);
    OPENSSL_cleanse(tag, sizeof tag);
    return ok ? 0 : -1;
}

static int open_and_hash(uint8_t hash[32], const uint8_t key[32], uint64_t counter,
                         const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *out) {
    if (ciphertext_len < AWG_TAG_LEN) return -1;
    uint8_t nonce[12];
    handshake_nonce(counter, nonce);
    size_t plain_len = ciphertext_len - AWG_TAG_LEN;
    int ok = rc_chacha20poly1305_open(key, nonce, hash, 32, ciphertext, plain_len,
                                      ciphertext + plain_len, out) == RC_OK;
    if (ok) ok = mix_hash(hash, ciphertext, ciphertext_len) == 0;
    OPENSSL_cleanse(nonce, sizeof nonce);
    return ok ? 0 : -1;
}

static uint32_t random_u32(void) {
    uint8_t bytes[4];
    if (RAND_bytes(bytes, sizeof bytes) != 1) return 1;
    uint32_t value = read_le32(bytes);
    return value ? value : 1;
}

static uint32_t choose_header(uint32_t min, uint32_t max) {
    if (min == max) return min;
    uint64_t span = (uint64_t)max - min + 1;
    return min + (uint32_t)((uint64_t)random_u32() % span);
}

static int random_bytes(uint8_t *out, size_t len) {
    return len <= INT_MAX && RAND_bytes(out, (int)len) == 1 ? 0 : -1;
}

static int make_tai64n(uint8_t out[12]) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return -1;
    uint64_t seconds = (uint64_t)tv.tv_sec + 0x400000000000000aULL;
    uint32_t nanos = (uint32_t)tv.tv_usec * 1000U;
    for (size_t i = 0; i < 8; ++i) out[i] = (uint8_t)(seconds >> (56 - 8 * i));
    out[8] = (uint8_t)(nanos >> 24);
    out[9] = (uint8_t)(nanos >> 16);
    out[10] = (uint8_t)(nanos >> 8);
    out[11] = (uint8_t)nanos;
    return 0;
}

void awg_handshake_init(awg_handshake_t *hs, const awg_config_t *cfg) {
    if (!hs) return;
    memset(hs, 0, sizeof *hs);
    hs->cfg = cfg;
}

awg_hs_status_t awg_handshake_build_initiation(awg_handshake_t *hs,
                                                uint8_t *out, size_t cap,
                                                size_t *out_len) {
    if (!hs || !hs->cfg || !out || !out_len) return AWG_HS_ERR_ARG;
    const awg_config_t *cfg = hs->cfg;
    size_t offset = cfg->padding[0];
    size_t need = offset + AWG_INIT_PACKET_LEN;
    if (need > cap || need > cfg->mtu) return AWG_HS_ERR_SPACE;
    if (random_bytes(out, offset) != 0) return AWG_HS_ERR_CRYPTO;

    hs->debug_stage = 1;
    if (blake2s(k_noise_name, sizeof k_noise_name - 1, hs->chain_key) != 0) return AWG_HS_ERR_CRYPTO;
    memcpy(hs->hash, hs->chain_key, sizeof hs->hash);
    hs->debug_stage = 2;
    if (mix_hash(hs->hash, k_wireguard_identifier, sizeof k_wireguard_identifier - 1) != 0 ||
        mix_hash(hs->hash, cfg->peer_public_key, AWG_KEY_LEN) != 0 ||
        rc_x25519_keygen(hs->ephemeral_private, hs->ephemeral_public) != RC_OK)
        return AWG_HS_ERR_CRYPTO;

    uint8_t *packet = out + offset;
    uint8_t key[32];
    uint8_t shared[32];
    uint8_t static_public[32];
    uint8_t timestamp[12];
    uint8_t mac_key[32];
    uint8_t mac_input[40];
    uint8_t mac[32];
    int ok = 1;
    hs->sender_index = random_u32();
    write_le32(packet, choose_header(cfg->header_min[0], cfg->header_max[0]));
    write_le32(packet + 4, hs->sender_index);
    memcpy(packet + 8, hs->ephemeral_public, 32);
    hs->debug_stage = 3;
    ok = mix_key(hs->chain_key, hs->ephemeral_public, AWG_KEY_LEN, key) == 0 &&
         mix_hash(hs->hash, hs->ephemeral_public, 32) == 0 &&
         rc_x25519_shared(hs->ephemeral_private, cfg->peer_public_key, shared) == RC_OK &&
         mix_key(hs->chain_key, shared, sizeof shared, key) == 0 &&
         rc_x25519_public(cfg->private_key, static_public) == RC_OK &&
         seal_and_hash(hs->hash, key, 0, static_public, AWG_KEY_LEN, packet + 40) == 0 &&
         rc_x25519_shared(cfg->private_key, cfg->peer_public_key, shared) == RC_OK &&
         mix_key(hs->chain_key, shared, sizeof shared, key) == 0 &&
         make_tai64n(timestamp) == 0 &&
         seal_and_hash(hs->hash, key, 0, timestamp, sizeof timestamp, packet + 88) == 0;
    hs->debug_stage = 4;
    if (ok) {
        memcpy(mac_input, k_mac1_label, sizeof k_mac1_label - 1);
        memcpy(mac_input + sizeof k_mac1_label - 1, cfg->peer_public_key, AWG_KEY_LEN);
        ok = blake2s(mac_input, sizeof mac_input, mac_key) == 0 &&
             blake2s_keyed_len(mac_key, sizeof mac_key, packet, 116, mac, AWG_TAG_LEN) == 0;
    }
    if (ok) {
        memcpy(packet + 116, mac, AWG_TAG_LEN);
        memset(packet + 132, 0, AWG_TAG_LEN);
        *out_len = need;
    }
    OPENSSL_cleanse(key, sizeof key);
    OPENSSL_cleanse(shared, sizeof shared);
    OPENSSL_cleanse(static_public, sizeof static_public);
    OPENSSL_cleanse(timestamp, sizeof timestamp);
    OPENSSL_cleanse(mac_key, sizeof mac_key);
    OPENSSL_cleanse(mac_input, sizeof mac_input);
    OPENSSL_cleanse(mac, sizeof mac);
    return ok ? AWG_HS_OK : AWG_HS_ERR_CRYPTO;
}

awg_hs_status_t awg_handshake_consume_response(awg_handshake_t *hs,
                                                const uint8_t *packet, size_t packet_len) {
    if (!hs || !hs->cfg || !packet) return AWG_HS_ERR_ARG;
    const awg_config_t *cfg = hs->cfg;
    size_t offset = cfg->padding[1];
    if (packet_len < offset + AWG_RESP_PACKET_LEN) return AWG_HS_ERR_FORMAT;
    const uint8_t *msg = packet + offset;
    uint32_t header = read_le32(msg);
    if (header < cfg->header_min[1] || header > cfg->header_max[1] ||
        read_le32(msg + 8) != hs->sender_index)
        return AWG_HS_ERR_FORMAT;

    uint8_t shared[32];
    uint8_t hash_key[32];
    uint8_t response_key[32];
    uint8_t plain[1];
    uint8_t zero_psk[32];
    memset(zero_psk, 0, sizeof zero_psk);
    const uint8_t *psk = cfg->has_preshared_key ? cfg->preshared_key : zero_psk;
    int ok = mix_key(hs->chain_key, msg + 12, AWG_KEY_LEN, response_key) == 0 &&
             mix_hash(hs->hash, msg + 12, 32) == 0 &&
             rc_x25519_shared(hs->ephemeral_private, msg + 12, shared) == RC_OK &&
             mix_key(hs->chain_key, shared, sizeof shared, response_key) == 0 &&
             rc_x25519_shared(cfg->private_key, msg + 12, shared) == RC_OK &&
             mix_key(hs->chain_key, shared, sizeof shared, response_key) == 0 &&
             kdf3(hs->chain_key, psk, 32, hs->chain_key, hash_key, response_key) == 0 &&
             mix_hash(hs->hash, hash_key, sizeof hash_key) == 0 &&
             open_and_hash(hs->hash, response_key, 0, msg + 44, AWG_TAG_LEN, plain) == 0;
    if (ok) {
        ok = kdf2(hs->chain_key, NULL, 0, hs->send_key, hs->recv_key) == 0;
    }
    if (ok) {
        hs->receiver_index = read_le32(msg + 4);
        hs->established = 1;
    }
    OPENSSL_cleanse(shared, sizeof shared);
    OPENSSL_cleanse(hash_key, sizeof hash_key);
    OPENSSL_cleanse(response_key, sizeof response_key);
    OPENSSL_cleanse(plain, sizeof plain);
    OPENSSL_cleanse(zero_psk, sizeof zero_psk);
    return ok ? AWG_HS_OK : AWG_HS_ERR_AUTH;
}

static int parse_decimal(const char *start, const char *end, size_t *out) {
    if (start == end) return -1;
    size_t value = 0;
    for (const char *p = start; p < end; ++p) {
        if (*p < '0' || *p > '9' || value > (SIZE_MAX - 9) / 10) return -1;
        value = value * 10 + (size_t)(*p - '0');
    }
    *out = value;
    return 0;
}

static int append_signature_bytes(const char *hex, size_t hex_len,
                                  uint8_t *out, size_t cap, size_t *pos) {
    if ((hex_len & 1) != 0 || *pos + hex_len / 2 > cap) return -1;
    for (size_t i = 0; i < hex_len; i += 2) {
        unsigned char hi = (unsigned char)hex[i];
        unsigned char lo = (unsigned char)hex[i + 1];
        hi = (unsigned char)(hi >= '0' && hi <= '9' ? hi - '0' :
                             hi >= 'a' && hi <= 'f' ? hi - 'a' + 10 :
                             hi >= 'A' && hi <= 'F' ? hi - 'A' + 10 : 0xff);
        lo = (unsigned char)(lo >= '0' && lo <= '9' ? lo - '0' :
                             lo >= 'a' && lo <= 'f' ? lo - 'a' + 10 :
                             lo >= 'A' && lo <= 'F' ? lo - 'A' + 10 : 0xff);
        if (hi == 0xff || lo == 0xff) return -1;
        out[(*pos)++] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

awg_hs_status_t awg_signature_expand(const char *spec, uint8_t *out, size_t cap,
                                     size_t *out_len) {
    if (!spec || !out || !out_len) return AWG_HS_ERR_ARG;
    size_t pos = 0;
    const char *p = spec;
    while (*p) {
        if (*p++ != '<') return AWG_HS_ERR_FORMAT;
        const char *end = strchr(p, '>');
        if (!end) return AWG_HS_ERR_FORMAT;
        const char *body = p;
        while (body < end && *body == ' ') ++body;
        if (end - body >= 4 && memcmp(body, "b 0x", 4) == 0) {
            if (append_signature_bytes(body + 4, (size_t)(end - body - 4), out, cap, &pos) != 0)
                return AWG_HS_ERR_SPACE;
        } else {
            const char *number = body;
            int kind = 0;
            if (end - body >= 3 && memcmp(body, "rd ", 3) == 0) {
                kind = 2; number += 3;
            } else if (end - body >= 3 && memcmp(body, "rc ", 3) == 0) {
                kind = 3; number += 3;
            } else if (end - body >= 2 && memcmp(body, "r ", 2) == 0) {
                kind = 1; number += 2;
            } else if (end - body == 1 && body[0] == 't') {
                if (pos + 4 > cap) return AWG_HS_ERR_SPACE;
                uint32_t now = (uint32_t)time(NULL);
                out[pos++] = (uint8_t)(now >> 24);
                out[pos++] = (uint8_t)(now >> 16);
                out[pos++] = (uint8_t)(now >> 8);
                out[pos++] = (uint8_t)now;
                p = end + 1;
                continue;
            } else {
                return AWG_HS_ERR_FORMAT;
            }
            size_t count = 0;
            if (parse_decimal(number, end, &count) != 0 || count > cap - pos) return AWG_HS_ERR_SPACE;
            if (kind == 1 && random_bytes(out + pos, count) != 0) return AWG_HS_ERR_CRYPTO;
            for (size_t i = 0; i < count; ++i) {
                if (kind == 2) out[pos + i] = (uint8_t)('0' + random_u32() % 10);
                else if (kind == 3) {
                    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
                    out[pos + i] = (uint8_t)alphabet[random_u32() % (sizeof alphabet - 1)];
                }
            }
            pos += count;
        }
        p = end + 1;
    }
    *out_len = pos;
    return AWG_HS_OK;
}

static long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

static void set_reason(char *reason, size_t cap, const char *text) {
    if (reason && cap) snprintf(reason, cap, "%s", text);
}

static awg_hs_status_t send_obfuscation(int fd, const awg_config_t *cfg,
                                        char *reason, size_t reason_cap) {
    static uint8_t packet[AWG_DATAGRAM_MAX];
    for (size_t i = 0; i < 5; ++i) {
        if (!cfg->signature[i][0]) continue;
        size_t len = 0;
        awg_hs_status_t r = awg_signature_expand(cfg->signature[i], packet, sizeof packet, &len);
        if (r != AWG_HS_OK) {
            if (reason && reason_cap)
                snprintf(reason, reason_cap, "signature I%zu cannot be expanded", i + 1);
            return r;
        }
        if (len && send(fd, packet, len, 0) != (ssize_t)len) {
            set_reason(reason, reason_cap, "signature udp send failed");
            return AWG_HS_ERR_IO;
        }
    }
    uint32_t span = cfg->jmax - cfg->jmin + 1;
    for (uint32_t i = 0; i < cfg->jc; ++i) {
        size_t len = cfg->jmin + (span > 1 ? random_u32() % span : 0);
        if (random_bytes(packet, len) != 0) {
            set_reason(reason, reason_cap, "junk packet random generation failed");
            return AWG_HS_ERR_CRYPTO;
        }
        if (send(fd, packet, len, 0) != (ssize_t)len) {
            set_reason(reason, reason_cap, "junk udp send failed");
            return AWG_HS_ERR_IO;
        }
    }
    return AWG_HS_OK;
}

awg_hs_status_t awg_handshake_establish_fd(int fd, const awg_config_t *cfg,
                                           int timeout_ms, awg_handshake_t *hs,
                                           char *reason, size_t reason_cap) {
    if (fd < 0 || !cfg || !hs) return AWG_HS_ERR_ARG;
    if (reason && reason_cap) reason[0] = '\0';
    awg_handshake_init(hs, cfg);
    uint8_t *initial = malloc(AWG_DATAGRAM_MAX);
    uint8_t *response = malloc(AWG_DATAGRAM_MAX);
    if (!initial || !response) {
        free(initial);
        free(response);
        set_reason(reason, reason_cap, "handshake memory allocation failed");
        return AWG_HS_ERR_CRYPTO;
    }
    size_t initial_len = 0;
    awg_hs_status_t r = send_obfuscation(fd, cfg, reason, reason_cap);
    if (r == AWG_HS_OK)
        r = awg_handshake_build_initiation(hs, initial, AWG_DATAGRAM_MAX, &initial_len);
    if (r == AWG_HS_ERR_SPACE && (!reason || !reason[0]) && reason && reason_cap)
        snprintf(reason, reason_cap, "initiation size=%lu mtu=%u s1=%u cap=%u",
                 (unsigned long)(AWG_INIT_PACKET_LEN + cfg->padding[0]),
                 (unsigned)cfg->mtu, (unsigned)cfg->padding[0], AWG_DATAGRAM_MAX);
    if (r == AWG_HS_ERR_CRYPTO && (!reason || !reason[0]) && reason && reason_cap)
        snprintf(reason, reason_cap, "initiation crypto stage %d", hs->debug_stage);
    if (r == AWG_HS_OK && send(fd, initial, initial_len, 0) != (ssize_t)initial_len)
        r = AWG_HS_ERR_IO;

    long deadline = now_ms() + (timeout_ms > 0 ? timeout_ms : 5000);
    while (r == AWG_HS_OK && now_ms() < deadline) {
        long remaining = deadline - now_ms();
        struct pollfd pfd = { fd, POLLIN, 0 };
        int pr = poll(&pfd, 1, remaining > 250 ? 250 : (int)remaining);
        if (pr < 0 && errno == EINTR) continue;
        if (pr < 0) { r = AWG_HS_ERR_IO; break; }
        if (pr == 0) continue;
        ssize_t got = recv(fd, response, AWG_DATAGRAM_MAX, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) { r = AWG_HS_ERR_IO; break; }
        ++hs->received_datagrams;
        r = awg_handshake_consume_response(hs, response, (size_t)got);
        if (r == AWG_HS_ERR_FORMAT) {
            ++hs->ignored_datagrams;
            hs->last_unrelated_len = (uint32_t)got;
            hs->last_unrelated_type = got >= 4 ? read_le32(response) : 0;
            r = AWG_HS_OK;
        }
    }
    int established = hs->established;
    if (r == AWG_HS_OK && !established) r = AWG_HS_ERR_TIMEOUT;
    if (r == AWG_HS_OK) set_reason(reason, reason_cap, "handshake accepted");
    else if (r == AWG_HS_ERR_TIMEOUT && hs->received_datagrams == 0)
        set_reason(reason, reason_cap, "handshake timed out without udp response");
    else if (r == AWG_HS_ERR_TIMEOUT) {
        if (reason && reason_cap)
            snprintf(reason, reason_cap, "handshake timed out after %u unrelated udp responses (type=%u len=%u)",
                     hs->ignored_datagrams, hs->last_unrelated_type, hs->last_unrelated_len);
    }
    else if (r == AWG_HS_ERR_AUTH) set_reason(reason, reason_cap, "handshake authentication failed");
    else if (r == AWG_HS_ERR_FORMAT) set_reason(reason, reason_cap, "invalid response packet");
    else if (r == AWG_HS_ERR_IO) set_reason(reason, reason_cap, "udp transport failed");
    else if (r == AWG_HS_ERR_SPACE && (!reason || !reason[0]))
        set_reason(reason, reason_cap, "awg packet exceeds mtu");
    else if (!reason || !reason[0]) set_reason(reason, reason_cap, "handshake crypto failed");
    OPENSSL_cleanse(initial, AWG_DATAGRAM_MAX);
    OPENSSL_cleanse(response, AWG_DATAGRAM_MAX);
    free(initial);
    free(response);
    return r;
}

awg_hs_status_t awg_handshake_probe(const awg_config_t *cfg, int timeout_ms,
                                    char *reason, size_t reason_cap) {
    if (!cfg || !cfg->endpoint_host[0] || !cfg->endpoint_port) return AWG_HS_ERR_ARG;
    char port[8];
    snprintf(port, sizeof port, "%u", cfg->endpoint_port);
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_UNSPEC;
    if (getaddrinfo(cfg->endpoint_host, port, &hints, &res) != 0 || !res) {
        set_reason(reason, reason_cap, "endpoint dns failed");
        return AWG_HS_ERR_IO;
    }
    awg_hs_status_t r = AWG_HS_ERR_IO;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0 || connect(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
            if (fd >= 0) close(fd);
            continue;
        }
        awg_handshake_t hs;
        r = awg_handshake_establish_fd(fd, cfg, timeout_ms, &hs, reason, reason_cap);
        OPENSSL_cleanse(&hs, sizeof hs);
        close(fd);
        if (r == AWG_HS_OK) {
            freeaddrinfo(res);
            return r;
        }
    }
    freeaddrinfo(res);
    return r;
}
