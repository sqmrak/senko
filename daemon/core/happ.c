#include "happ.h"
#include "b64.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "happ_keys.inc"

/* declared in config.h; avoid circular include */
int url_percent_decode(const char *src, size_t src_len, char *dst, size_t cap);

/* url-safe b64 alphabet; reuse project decoder after normalize */
static int happ_b64_decode(const char *in, size_t in_len,
                           unsigned char *out, size_t cap, size_t *out_len) {
    char *norm;
    size_t i, n;
    int rc;

    if (!in || !out || !out_len) return -1;
    norm = (char *)malloc(in_len + 4);
    if (!norm) return -1;
    n = 0;
    for (i = 0; i < in_len; ++i) {
        char c = in[i];
        if (c == '-' ) c = '+';
        else if (c == '_') c = '/';
        else if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        norm[n++] = c;
    }
    while (n % 4) norm[n++] = '=';
    norm[n] = '\0';
    rc = b64_decode(norm, n, out, cap, out_len);
    free(norm);
    return rc;
}

static RSA *load_pkcs1_b64(const char *b64) {
    BIO *bio = NULL;
    RSA *rsa = NULL;
    char *pem = NULL;
    size_t blen, pem_cap, o = 0;
    size_t i;

    if (!b64) return NULL;
    blen = strlen(b64);
/* pem wrapper + 64-col wraps */
    pem_cap = blen + blen / 64 + 128;
    pem = (char *)malloc(pem_cap);
    if (!pem) return NULL;

    o += (size_t)snprintf(pem + o, pem_cap - o, "-----BEGIN RSA PRIVATE KEY-----\n");
    for (i = 0; i < blen && o + 66 < pem_cap; i += 64) {
        size_t chunk = blen - i;
        if (chunk > 64) chunk = 64;
        memcpy(pem + o, b64 + i, chunk);
        o += chunk;
        pem[o++] = '\n';
    }
    o += (size_t)snprintf(pem + o, pem_cap - o, "-----END RSA PRIVATE KEY-----\n");

    bio = BIO_new_mem_buf(pem, (int)o);
    if (!bio) { free(pem); return NULL; }
    rsa = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    free(pem);
    return rsa;
}

/* rsa-pkcs1v15 blocks for crypt1-4 */
static int decrypt_crypt_rsa(int ordinal, const char *payload, char *out, size_t out_cap) {
    RSA *rsa = NULL;
    unsigned char *cipher = NULL;
    unsigned char *block = NULL;
    size_t clen = 0, cap, pos, o = 0;
    int ksz, n;

    if (ordinal < 0 || ordinal > 3 || !payload || !out || out_cap < 2)
        return -1;

    rsa = load_pkcs1_b64(kHappPkcs1B64[ordinal]);
    if (!rsa) return -1;
    ksz = RSA_size(rsa);
    if (ksz <= 0) { RSA_free(rsa); return -1; }

    cap = b64_decoded_maxlen(strlen(payload)) + 16;
    cipher = (unsigned char *)malloc(cap ? cap : 1);
    block = (unsigned char *)malloc((size_t)ksz + 1);
    if (!cipher || !block) {
        free(cipher); free(block); RSA_free(rsa);
        return -1;
    }
    if (happ_b64_decode(payload, strlen(payload), cipher, cap, &clen) != 0) {
        free(cipher); free(block); RSA_free(rsa);
        return -1;
    }

    for (pos = 0; pos < clen; pos += (size_t)ksz) {
        size_t left = clen - pos;
        size_t take = left < (size_t)ksz ? left : (size_t)ksz;
        if (take < (size_t)ksz) break; /* incomplete tail */
        n = RSA_private_decrypt((int)take, cipher + pos, block, rsa, RSA_PKCS1_PADDING);
        if (n <= 0) {
            free(cipher); free(block); RSA_free(rsa);
            return -1;
        }
        if (o + (size_t)n + 1 > out_cap) {
            free(cipher); free(block); RSA_free(rsa);
            return -1;
        }
        memcpy(out + o, block, (size_t)n);
        o += (size_t)n;
    }

    out[o] = '\0';
    free(cipher);
    free(block);
    RSA_free(rsa);
    return o > 0 ? 0 : -1;
}

/* strip optional whitespace and trailing junk */
static void trim_inplace(char *s) {
    char *e;
    size_t n;
    if (!s) return;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        memmove(s, s + 1, strlen(s));
    n = strlen(s);
    e = s + n;
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = '\0';
}

int happ_unwrap(const char *uri, char *out, size_t out_cap) {
    const char *p;
    int ordinal = -1;
    size_t prefix = 0;

    if (!uri || !out || out_cap < 8) return -1;
    out[0] = '\0';

    p = uri;
    if (strncmp(p, "happ://", 7) == 0)
        p += 7;
    else if (strncmp(p, "HAPP://", 7) == 0)
        p += 7;
    else
        return -1;

/* crypt5 needs chacha keytable; refuse clearly */
    if (strncmp(p, "crypt5/", 7) == 0)
        return -1;

    if (strncmp(p, "crypt4/", 7) == 0) { ordinal = 3; prefix = 7; }
    else if (strncmp(p, "crypt3/", 7) == 0) { ordinal = 2; prefix = 7; }
    else if (strncmp(p, "crypt2/", 7) == 0) { ordinal = 1; prefix = 7; }
    else if (strncmp(p, "crypt/", 6) == 0)  { ordinal = 0; prefix = 6; }
    else {
/* plain body after happ:// - percent-decode or base64 of a link list */
        size_t plen = strlen(p);
        if (plen == 0) return -1;
        if (url_percent_decode(p, plen, out, out_cap) < 0) {
/* try raw copy */
            if (plen + 1 > out_cap) return -1;
            memcpy(out, p, plen + 1);
        }
        trim_inplace(out);
/* if still not a scheme, try b64 */
        if (!strstr(out, "://")) {
            size_t dlen = 0;
            size_t cap = b64_decoded_maxlen(plen);
            unsigned char *buf;
            if (cap + 1 > out_cap) cap = out_cap - 1;
            buf = (unsigned char *)malloc(cap ? cap : 1);
            if (!buf) return -1;
            if (happ_b64_decode(p, plen, buf, cap, &dlen) == 0 && dlen > 0) {
                if (dlen >= out_cap) dlen = out_cap - 1;
                memcpy(out, buf, dlen);
                out[dlen] = '\0';
            }
            free(buf);
        }
        trim_inplace(out);
        return out[0] ? 0 : -1;
    }

    if (decrypt_crypt_rsa(ordinal, p + prefix, out, out_cap) != 0)
        return -1;
    trim_inplace(out);
    return out[0] ? 0 : -1;
}
