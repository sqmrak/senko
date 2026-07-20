#define _DEFAULT_SOURCE

#include "reality_handshake.h"
#include "senko_trace.h"

#include "reality_crypto.h"
#include "reality_auth.h"
#include "tls_clienthello.h"
#include "tls13_transcript.h"
#include "tls13_keysched.h"
#include "tls13_kdf.h"
#include "tls13_handshake.h"
#include "tls13_record.h"
#include "b64.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>

#define CT_CCS         20
#define CT_ALERT       21
#define CT_HANDSHAKE   22
#define CT_APPDATA     23

#define HS_SERVER_HELLO        2
#define HS_NEW_SESSION_TICKET  4
#define HS_ENCRYPTED_EXTS      8
#define HS_CERTIFICATE         11
#define HS_CERT_VERIFY         15
#define HS_FINISHED            20
#define HS_KEY_UPDATE          24

#define MAX_RECORD   (16384 + 256) /* tls record cap + aead overhead slack */
/* utls/reality peers expect ~mss-sized app records early on */
#define RH_APP_PLAIN_CAP 1186
#define RH_APP_BULK_CAP  16384
#define RH_APP_BULK_AFTER  (128 * 1024)
#define FLIGHT_CAP   (64 * 1024) /* reassembled server flight buffer */

static const char *rh_status_name(rh_status_t st) {
    switch (st) {
        case RH_OK:              return "ok";
        case RH_ERR_ARG:         return "arg";
        case RH_ERR_IO:          return "io";
        case RH_ERR_PROTO:       return "proto";
        case RH_ERR_CRYPTO:      return "crypto";
        case RH_ERR_SUITE:       return "suite";
        case RH_ERR_NOT_REALITY: return "not_reality";
        default:                 return "unknown";
    }
}

typedef struct {
    int      fd;
    tls13_aead_t aead; /* negotiated: aes-128-gcm or chacha20 */
    size_t   key_len; /* 16 aes-128 / 32 chacha */
/* retain traffic secrets for tls 1.3 key updates */
    uint8_t  c_ap_secret[TLS13_HASH_LEN];
    uint8_t  s_ap_secret[TLS13_HASH_LEN];
    uint8_t  c_app_key[TLS13_REC_MAXKEY_LEN], c_app_iv[TLS13_IV_LEN];
    uint8_t  s_app_key[TLS13_REC_MAXKEY_LEN], s_app_iv[TLS13_IV_LEN];
    uint64_t c_app_seq, s_app_seq;
/* decrypted app plaintext ready for the session layer */
    uint8_t  rxbuf[MAX_RECORD];
    size_t   rxlen, rxoff;
/* retain partial records so eagain cannot desync aead or truncate downloads */
    uint8_t  rdhdr[5];
    size_t   rdhdr_got;
    uint8_t  rdbody[MAX_RECORD];
    size_t   rdbody_need, rdbody_got;
    int      rd_have_hdr;
/* keep ciphertext queued while pollout is blocked */
    uint8_t  wpend[MAX_RECORD];
    size_t   wpend_len, wpend_off;
/* defer key update replies until the pending record drains */
    int      ku_reply_pending;
/* switch to raw bytes only after authenticated vision direct mode */
    int      write_plain;
    int      read_plain;
    uint64_t c_app_bytes; /* sealed app bytes; gates bulk record size */
} rh_conn_t;

/* keep bootstrap blocking because free nodes often stall mid-flight */

#define RH_HS_IO_POLL_MS  2000
#define RH_HS_IO_BUDGET_MS 5000

static int read_full(int fd, uint8_t *buf, size_t n, int *want_read) {
    size_t off = 0;
    int waited = 0;
    while (off < n) {
        ssize_t r = read(fd, buf + off, n - off);
        if (r > 0) { off += (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (waited >= RH_HS_IO_BUDGET_MS) {
                if (want_read && off == 0) *want_read = 1;
                return -1;
            }
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLIN;
            int slice = RH_HS_IO_POLL_MS;
            if (slice > RH_HS_IO_BUDGET_MS - waited)
                slice = RH_HS_IO_BUDGET_MS - waited;
            int pr = poll(&pfd, 1, slice);
            if (pr > 0) { waited += slice; continue; }
            if (pr == 0) { waited += slice; continue; }
        }
        return -1;
    }
    return 0;
}

static int write_full(int fd, const uint8_t *buf, size_t n) {
    size_t off = 0;
    int waited = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w > 0) { off += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (waited >= RH_HS_IO_BUDGET_MS) return -1;
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            int slice = RH_HS_IO_POLL_MS;
            if (slice > RH_HS_IO_BUDGET_MS - waited)
                slice = RH_HS_IO_BUDGET_MS - waited;
            int pr = poll(&pfd, 1, slice);
            if (pr > 0) { waited += slice; continue; }
            if (pr == 0) { waited += slice; continue; }
        }
        return -1;
    }
    return 0;
}

static int read_record(int fd, uint8_t *type, uint8_t *body, size_t cap, size_t *body_len, int *want_read) {
    uint8_t hdr[5];
    if (read_full(fd, hdr, 5, want_read) != 0) return -1;
    size_t len = ((size_t)hdr[3] << 8) | hdr[4];
    if (len > cap) return -1;
    if (read_full(fd, body, len, want_read) != 0) return -1;
    *type = hdr[0];
    *body_len = len;
    return 0;
}

static int write_plaintext_record(int fd, uint8_t type, const uint8_t *data, size_t len) {
    uint8_t hdr[5] = { type, 0x03, 0x03, (uint8_t)(len >> 8), (uint8_t)(len & 0xff) };
    if (write_full(fd, hdr, 5) != 0) return -1;
    return write_full(fd, data, len);
}

/* collect encrypted handshake fragments while ignoring plaintext ccs records */

typedef struct {
    uint8_t  buf[FLIGHT_CAP];
    size_t   len;
    const uint8_t *s_key, *s_iv;
    tls13_aead_t aead;
    uint64_t seq;
    int      fd;
} flight_t;

/* collect one server flight while preserving io and auth failure classes */
static int flight_fill(flight_t *f) {
    for (;;) {
        uint8_t rec[MAX_RECORD], type;
        size_t rlen;
        if (read_record(f->fd, &type, rec, sizeof rec, &rlen, NULL) != 0) return -1;
        if (type == CT_CCS) continue; /* middlebox-compat, ignore */
        if (type == CT_ALERT) return -1;
        if (type != CT_APPDATA) return -1; /* expected encrypted record */

        uint8_t plain[MAX_RECORD]; size_t plen = 0; uint8_t inner = 0;
        uint8_t full[5 + MAX_RECORD];
        full[0] = type; full[1] = 0x03; full[2] = 0x03;
        full[3] = (uint8_t)(rlen >> 8); full[4] = (uint8_t)(rlen & 0xff);
        memcpy(full + 5, rec, rlen);
        tls13_rec_status_t rs = tls13_record_open_suite(f->aead, f->s_key, f->s_iv, f->seq,
                                                  full, 5 + rlen,
                                                  plain, sizeof plain, &plen, &inner);
        if (rs != TLS13_REC_OK) return -2;
        f->seq++;
        if (inner == CT_ALERT) return -1;
        if (inner != CT_HANDSHAKE) continue; /* ignore non-handshake */
        if (f->len + plen > sizeof f->buf) return -1;
        memcpy(f->buf + f->len, plain, plen);
        f->len += plen;
        return 0;
    }
}

static int flight_next_msg(flight_t *f, size_t *consumed_base,
                           const uint8_t **msg, size_t *msg_len) {
    for (;;) {
        size_t avail = f->len - *consumed_base;
        if (avail >= 4) {
            const uint8_t *m = f->buf + *consumed_base;
            size_t body = ((size_t)m[1] << 16) | ((size_t)m[2] << 8) | m[3];
            if (avail >= 4 + body) {
                *msg = m;
                *msg_len = 4 + body;
                *consumed_base += 4 + body;
                return 0;
            }
        }
        int r = flight_fill(f);
        if (r != 0) return r;
    }
}

/* use openssl for the leaf because an in-house x509 parser would add risk */
static int cert_extract(const uint8_t *msg, size_t msg_len,
                        uint8_t pub_out[32], uint8_t *sig_out, size_t sig_cap,
                        size_t *sig_len) {
    if (msg_len < 4) return -1;
    const uint8_t *p = msg + 4;
    size_t left = msg_len - 4;
    if (left < 1) return -1;
    size_t ctx_len = p[0];
    if (1 + ctx_len + 3 > left) return -1;
    p += 1 + ctx_len; left -= 1 + ctx_len;
    size_t list_len = ((size_t)p[0] << 16) | ((size_t)p[1] << 8) | p[2];
    p += 3; left -= 3;
    if (list_len > left) return -1;
    if (list_len < 3) return -1;
    size_t cert_len = ((size_t)p[0] << 16) | ((size_t)p[1] << 8) | p[2];
    p += 3;
    if (cert_len + 2 > list_len - 0) return -1; /* need cert + ext len */
    const uint8_t *der = p;

    const unsigned char *dp = der;
    X509 *x = d2i_X509(NULL, &dp, (long)cert_len);
    if (!x) return -1;

    int rc = -1;
    EVP_PKEY *pk = X509_get0_pubkey(x);
    if (pk) {
        size_t plen = 32;
        if (EVP_PKEY_get_raw_public_key(pk, pub_out, &plen) == 1 && plen == 32) {
            const ASN1_BIT_STRING *psig = NULL;
            const X509_ALGOR *palg = NULL;
            X509_get0_signature(&psig, &palg, x);
            if (psig && psig->data && (size_t)psig->length <= sig_cap) {
                memcpy(sig_out, psig->data, (size_t)psig->length);
                *sig_len = (size_t)psig->length;
                rc = 0;
            }
        }
    }
    X509_free(x);
    return rc;
}

void *reality_handshake_open(int fd, const rh_params_t *p, rh_status_t *err) {
    rh_status_t e = RH_ERR_PROTO;
#define FAIL(code) do { e = (code); goto fail; } while (0)

    rh_conn_t *c = NULL;
    tls13_transcript_t tr; tr.ctx = NULL;
    uint8_t eph_priv[32], eph_pub[32];
    uint8_t authkey[RA_AUTHKEY_LEN];

    if (fd < 0 || !p) { e = RH_ERR_ARG; goto fail; }

    if (rc_x25519_keygen(eph_priv, eph_pub) != RC_OK) FAIL(RH_ERR_CRYPTO);

    uint8_t hello[2048]; size_t hello_len = 0;
    tls_ch_params_t chp;
    memset(&chp, 0, sizeof chp);
/* random must be real entropy: reality reads random[:20] as the hkdf salt and random[20:32] as the seal nonce */
    if (RAND_bytes(chp.random, TLS_CH_RANDOM_LEN) != 1) FAIL(RH_ERR_CRYPTO);
    memset(chp.session_id, 0, TLS_CH_SESSIONID_LEN);
    memcpy(chp.x25519_pub, eph_pub, 32);
    chp.sni = p->sni;
    chp.fp = (tls_fp_t)p->fp;
    chp.p256_pub = p->has_p256 ? p->p256_pub : NULL;
    if (tls_build_clienthello(&chp, hello, sizeof hello, &hello_len) != TLS_CH_OK)
        FAIL(RH_ERR_PROTO);

/* bind the reality token to the zeroed clienthello aad */
    if (reality_derive_authkey(eph_priv, p->pbk, chp.random, authkey) != RA_OK)
        FAIL(RH_ERR_CRYPTO);
    uint8_t sid[RA_SESSIONID_LEN];
    if (reality_seal_token(authkey, chp.random, p->version, (uint32_t)time(NULL),
                           p->short_id, p->short_id_len,
                           hello, hello_len, sid) != RA_OK)
        FAIL(RH_ERR_CRYPTO);
    memcpy(hello + TLS_CH_SESSIONID_OFF, sid, RA_SESSIONID_LEN);

    if (tls13_transcript_init(&tr) != 0) FAIL(RH_ERR_CRYPTO);
    tls13_transcript_update(&tr, hello, hello_len);

    if (write_plaintext_record(fd, CT_HANDSHAKE, hello, hello_len) != 0)
        FAIL(RH_ERR_IO);

    uint8_t shrec[MAX_RECORD], shtype; size_t shlen;
    do {
        if (read_record(fd, &shtype, shrec, sizeof shrec, &shlen, NULL) != 0) FAIL(RH_ERR_IO);
    } while (shtype == CT_CCS);
    if (shtype != CT_HANDSHAKE) FAIL(RH_ERR_PROTO);

    tls13_serverhello_t sh;
    if (tls13_parse_serverhello(shrec, shlen, &sh) != TLS13_SH_OK) FAIL(RH_ERR_PROTO);
/* both suites we accept ride the sha-256 key schedule */
    tls13_aead_t aead;
    size_t key_len;
    if (sh.cipher_suite == 0x1301)      { aead = TLS13_AEAD_AES128GCM; key_len = 16; }
    else if (sh.cipher_suite == 0x1303) { aead = TLS13_AEAD_CHACHA20;  key_len = 32; }
    else FAIL(RH_ERR_SUITE);
    if (!sh.have_key_share) FAIL(RH_ERR_PROTO);
    tls13_transcript_update(&tr, shrec, shlen);

    uint8_t ecdhe[RC_SHARED_LEN];
    if (rc_x25519_shared(eph_priv, sh.server_x25519, ecdhe) != RC_OK) FAIL(RH_ERR_CRYPTO);

    uint8_t early[TLS13_HASH_LEN], hs_secret[TLS13_HASH_LEN];
    if (tls13_early_secret(NULL, 0, early) != TLS13_OK) FAIL(RH_ERR_CRYPTO);
    if (tls13_handshake_secret(early, ecdhe, sizeof ecdhe, hs_secret) != TLS13_OK)
        FAIL(RH_ERR_CRYPTO);

    uint8_t th_chsh[TLS13_TRANSCRIPT_LEN]; /* hash(ch..sh) */
    if (tls13_transcript_current(&tr, th_chsh) != 0) FAIL(RH_ERR_CRYPTO);

    uint8_t s_hs_secret[TLS13_HASH_LEN], c_hs_secret[TLS13_HASH_LEN];
    if (tls13_traffic_secret(hs_secret, "s hs traffic", th_chsh, s_hs_secret) != TLS13_OK)
        FAIL(RH_ERR_CRYPTO);
    if (tls13_traffic_secret(hs_secret, "c hs traffic", th_chsh, c_hs_secret) != TLS13_OK)
        FAIL(RH_ERR_CRYPTO);

    uint8_t s_hs_key[TLS13_REC_MAXKEY_LEN], s_hs_iv[TLS13_IV_LEN];
    uint8_t c_hs_key[TLS13_REC_MAXKEY_LEN], c_hs_iv[TLS13_IV_LEN];
    if (tls13_traffic_keys_len(s_hs_secret, s_hs_key, key_len, s_hs_iv) != TLS13_OK) FAIL(RH_ERR_CRYPTO);
    if (tls13_traffic_keys_len(c_hs_secret, c_hs_key, key_len, c_hs_iv) != TLS13_OK) FAIL(RH_ERR_CRYPTO);

    flight_t fl;
    memset(&fl, 0, sizeof fl);
    fl.s_key = s_hs_key; fl.s_iv = s_hs_iv; fl.aead = aead; fl.seq = 0; fl.fd = fd;
    size_t fconsumed = 0;

    int saw_ee = 0, saw_cert = 0, saw_cv = 0;
    uint8_t cert_pub[32]; uint8_t cert_sig[128]; size_t cert_sig_len = 0;
    int have_cert = 0;
    uint8_t th_before_fin[TLS13_TRANSCRIPT_LEN]; /* hash up to cert verify */

    for (;;) {
        const uint8_t *msg; size_t mlen;
        int r = flight_next_msg(&fl, &fconsumed, &msg, &mlen);
        if (r == -2) FAIL(RH_ERR_CRYPTO);
        if (r != 0) FAIL(RH_ERR_PROTO);
        uint8_t mtype = msg[0];

        if (mtype == HS_FINISHED) {
            if (tls13_transcript_current(&tr, th_before_fin) != 0) FAIL(RH_ERR_CRYPTO);
            if (mlen != 4 + TLS13_FINISHED_LEN) FAIL(RH_ERR_PROTO);
            if (tls13_finished_verify(s_hs_secret, th_before_fin, msg + 4) != TLS13_OK)
                FAIL(RH_ERR_PROTO);
            tls13_transcript_update(&tr, msg, mlen); /* now include it */
            break;
        }

        if (mtype == HS_ENCRYPTED_EXTS) saw_ee = 1;
        else if (mtype == HS_CERTIFICATE) {
            saw_cert = 1;
            if (cert_extract(msg, mlen, cert_pub, cert_sig, sizeof cert_sig,
                             &cert_sig_len) == 0)
                have_cert = 1;
        }
        else if (mtype == HS_CERT_VERIFY) saw_cv = 1;
        tls13_transcript_update(&tr, msg, mlen);
    }
    (void)saw_ee; (void)saw_cv;
    if (!saw_cert || !have_cert) FAIL(RH_ERR_PROTO);

    if (reality_verify_server(authkey, cert_pub, 32, cert_sig, cert_sig_len) != RA_OK)
        FAIL(RH_ERR_NOT_REALITY);

    uint8_t master[TLS13_HASH_LEN];
    if (tls13_master_secret(hs_secret, master) != TLS13_OK) FAIL(RH_ERR_CRYPTO);
    uint8_t th_full[TLS13_TRANSCRIPT_LEN];
    if (tls13_transcript_current(&tr, th_full) != 0) FAIL(RH_ERR_CRYPTO);

    uint8_t s_ap_secret[TLS13_HASH_LEN], c_ap_secret[TLS13_HASH_LEN];
    if (tls13_traffic_secret(master, "s ap traffic", th_full, s_ap_secret) != TLS13_OK)
        FAIL(RH_ERR_CRYPTO);
    if (tls13_traffic_secret(master, "c ap traffic", th_full, c_ap_secret) != TLS13_OK)
        FAIL(RH_ERR_CRYPTO);

/* client finished covers the transcript through the server finished */
    uint8_t cfin_vd[TLS13_FINISHED_LEN];
    {
        uint8_t fk[TLS13_HASH_LEN];
        if (tls13_finished_key(c_hs_secret, fk) != TLS13_OK) FAIL(RH_ERR_CRYPTO);
        if (tls13_finished_mac(fk, th_full, cfin_vd) != TLS13_OK) FAIL(RH_ERR_CRYPTO);
    }
    uint8_t cfin_msg[4 + TLS13_FINISHED_LEN];
    cfin_msg[0] = HS_FINISHED; cfin_msg[1] = 0; cfin_msg[2] = 0;
    cfin_msg[3] = TLS13_FINISHED_LEN;
    memcpy(cfin_msg + 4, cfin_vd, TLS13_FINISHED_LEN);

    uint8_t cfin_rec[MAX_RECORD]; size_t cfin_rec_len = 0;
    if (tls13_record_seal_suite(aead, c_hs_key, c_hs_iv, 0, CT_HANDSHAKE,
                          cfin_msg, sizeof cfin_msg,
                          cfin_rec, sizeof cfin_rec, &cfin_rec_len) != TLS13_REC_OK)
        FAIL(RH_ERR_CRYPTO);
    if (write_full(fd, cfin_rec, cfin_rec_len) != 0) FAIL(RH_ERR_IO);

    c = (rh_conn_t *)calloc(1, sizeof *c);
    if (!c) FAIL(RH_ERR_CRYPTO);
    c->fd = fd;
    c->aead = aead;
    c->key_len = key_len;
    memcpy(c->c_ap_secret, c_ap_secret, TLS13_HASH_LEN);
    memcpy(c->s_ap_secret, s_ap_secret, TLS13_HASH_LEN);
    if (tls13_traffic_keys_len(c_ap_secret, c->c_app_key, key_len, c->c_app_iv) != TLS13_OK) FAIL(RH_ERR_CRYPTO);
    if (tls13_traffic_keys_len(s_ap_secret, c->s_app_key, key_len, c->s_app_iv) != TLS13_OK) FAIL(RH_ERR_CRYPTO);
    c->c_app_seq = 0; c->s_app_seq = 0;
    c->rxlen = c->rxoff = 0;

    tls13_transcript_free(&tr);
    if (err) *err = RH_OK;
    return c;

fail:
    if (tr.ctx) tls13_transcript_free(&tr);
    if (c) free(c);
    if (err) *err = e;
    return NULL;
#undef FAIL
}

/* the transport vtable starts after the reality handshake is live */

/* nonblocking fill: 1 complete, 0 need read, -1 hard err, -2 clean eof */
static int nb_fill(int fd, uint8_t *buf, size_t need, size_t *got) {
    while (*got < need) {
        ssize_t r = read(fd, buf + *got, need - *got);
        if (r > 0) { *got += (size_t)r; continue; }
        if (r == 0) return (*got == 0) ? -2 : -1; /* clean eof vs truncated */
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    return 1;
}

static int flush_wpend(rh_conn_t *c) {
    while (c->wpend_off < c->wpend_len) {
        ssize_t w = write(c->fd, c->wpend + c->wpend_off,
                          c->wpend_len - c->wpend_off);
        if (w > 0) { c->wpend_off += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return TRANSPORT_WANT_WRITE;
        return TRANSPORT_ERR;
    }
    c->wpend_off = c->wpend_len = 0;
    return 0;
}

/* pull one tls record into type/body without blocking the event loop */
static int nb_next_record(rh_conn_t *c, uint8_t *type,
                          uint8_t *body, size_t cap, size_t *body_len) {
    if (!c->rd_have_hdr) {
        int st = nb_fill(c->fd, c->rdhdr, 5, &c->rdhdr_got);
        if (st == 0) return TRANSPORT_WANT_READ;
        if (st == -2) return TRANSPORT_EOF;
        if (st < 0) return TRANSPORT_ERR;
        size_t len = ((size_t)c->rdhdr[3] << 8) | c->rdhdr[4];
        if (len > cap || len > sizeof c->rdbody) return TRANSPORT_ERR;
        c->rdbody_need = len;
        c->rdbody_got = 0;
        c->rd_have_hdr = 1;
    }
    int st = nb_fill(c->fd, c->rdbody, c->rdbody_need, &c->rdbody_got);
    if (st == 0) return TRANSPORT_WANT_READ;
    if (st == -2) return TRANSPORT_EOF;
    if (st < 0) return TRANSPORT_ERR;
    *type = c->rdhdr[0];
    *body_len = c->rdbody_need;
    if (*body_len) memcpy(body, c->rdbody, *body_len);
    c->rd_have_hdr = 0;
    c->rdhdr_got = 0;
    c->rdbody_need = c->rdbody_got = 0;
    return 0;
}

/* rotate traffic secrets when servers send tls 1.3 key updates */
static int rh_update_read_keys(rh_conn_t *c) {
    uint8_t next[TLS13_HASH_LEN];
    if (tls13_expand_label(c->s_ap_secret, "traffic upd", NULL, 0,
                           next, TLS13_HASH_LEN) != TLS13_OK)
        return -1;
    memcpy(c->s_ap_secret, next, TLS13_HASH_LEN);
    if (tls13_traffic_keys_len(c->s_ap_secret, c->s_app_key, c->key_len,
                               c->s_app_iv) != TLS13_OK)
        return -1;
    c->s_app_seq = 0;
    return 0;
}

static int rh_update_write_keys(rh_conn_t *c) {
    uint8_t next[TLS13_HASH_LEN];
    if (tls13_expand_label(c->c_ap_secret, "traffic upd", NULL, 0,
                           next, TLS13_HASH_LEN) != TLS13_OK)
        return -1;
    memcpy(c->c_ap_secret, next, TLS13_HASH_LEN);
    if (tls13_traffic_keys_len(c->c_ap_secret, c->c_app_key, c->key_len,
                               c->c_app_iv) != TLS13_OK)
        return -1;
    c->c_app_seq = 0;
    return 0;
}

/* queue key update replies so write ordering stays intact */
static int rh_queue_key_update_reply(rh_conn_t *c) {
/* encode update_not_requested as a post-handshake message */
    static const uint8_t ku_msg[5] = { HS_KEY_UPDATE, 0, 0, 1, 0 };
    if (c->wpend_len) return TRANSPORT_WANT_WRITE;
    size_t rlen = 0;
    if (tls13_record_seal_suite(c->aead, c->c_app_key, c->c_app_iv, c->c_app_seq,
                                CT_HANDSHAKE, ku_msg, sizeof ku_msg,
                                c->wpend, sizeof c->wpend, &rlen) != TLS13_REC_OK)
        return TRANSPORT_ERR;
    c->c_app_seq++;
    c->wpend_len = rlen;
    c->wpend_off = 0;
/* rotate write keys only after the reply is queued */
    if (rh_update_write_keys(c) != 0) return TRANSPORT_ERR;
    c->ku_reply_pending = 0;
    fprintf(stderr, "senkod: reality key_update reply sent\n");
    return 0;
}

/* handle key updates without exposing handshake records to the session */
static int rh_handle_post_hs(rh_conn_t *c, const uint8_t *msg, size_t len) {
    size_t off = 0;
    while (off + 4 <= len) {
        uint8_t type = msg[off];
        size_t mlen = ((size_t)msg[off + 1] << 16) |
                      ((size_t)msg[off + 2] << 8) |
                      (size_t)msg[off + 3];
        if (off + 4 + mlen > len) break;
        const uint8_t *body = msg + off + 4;
        if (type == HS_KEY_UPDATE) {
            if (mlen < 1) return -1;
            if (rh_update_read_keys(c) != 0) return -1;
            fprintf(stderr, "senkod: reality key_update applied (read)\n");
            if (body[0] == 1) /* update_requested */
                c->ku_reply_pending = 1;
        }
/* ignore tickets and other post-handshake messages */
        off += 4 + mlen;
    }
    return 0;
}

/* stage raw vision direct bytes when the socket blocks */
static int rh_write_plain(rh_conn_t *c, const uint8_t *buf, size_t len) {
    if (c->wpend_len) {
        int fr = flush_wpend(c);
        if (fr != 0) return fr;
    }
    if (len == 0) return 0;
    if (len > sizeof c->wpend) len = sizeof c->wpend;
    memcpy(c->wpend, buf, len);
    c->wpend_len = len;
    c->wpend_off = 0;
    int fr = flush_wpend(c);
    if (fr == TRANSPORT_ERR) return TRANSPORT_ERR;
    return (int)len;
}

static int rh_read_plain(rh_conn_t *c, uint8_t *buf, size_t len) {
    if (c->rxoff < c->rxlen) {
        size_t n = c->rxlen - c->rxoff;
        if (n > len) n = len;
        memcpy(buf, c->rxbuf + c->rxoff, n);
        c->rxoff += n;
        return (int)n;
    }
    if (c->wpend_len) {
        int fr = flush_wpend(c);
        if (fr == TRANSPORT_ERR) return TRANSPORT_ERR;
        if (fr == TRANSPORT_WANT_WRITE) return TRANSPORT_WANT_WRITE;
    }
    if (len == 0) return 0;
    ssize_t r = read(c->fd, buf, len);
    if (r > 0) return (int)r;
    if (r == 0) return TRANSPORT_EOF;
    if (errno == EINTR) return TRANSPORT_WANT_READ;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return TRANSPORT_WANT_READ;
    return TRANSPORT_ERR;
}

/* replay the consumed record when vision direct switches to a bare stream */
static int rh_enter_read_plain(rh_conn_t *c, uint8_t type,
                               const uint8_t *body, size_t body_len,
                               uint8_t *buf, size_t len) {
    if (5 + body_len > sizeof c->rxbuf) return TRANSPORT_ERR;
    c->read_plain = 1;
    c->rxbuf[0] = type;
    c->rxbuf[1] = 0x03;
    c->rxbuf[2] = 0x03;
    c->rxbuf[3] = (uint8_t)(body_len >> 8);
    c->rxbuf[4] = (uint8_t)(body_len & 0xff);
    if (body_len) memcpy(c->rxbuf + 5, body, body_len);
    c->rxlen = 5 + body_len;
    c->rxoff = 0;
    fprintf(stderr, "senkod: reality splice plain seq=%llu rlen=%zu\n",
            (unsigned long long)c->s_app_seq, body_len);
    return rh_read_plain(c, buf, len);
}

static int rh_read(void *handle, uint8_t *buf, size_t len) {
    rh_conn_t *c = (rh_conn_t *)handle;
    if (c->read_plain)
        return rh_read_plain(c, buf, len);
    if (c->rxoff < c->rxlen) {
        size_t n = c->rxlen - c->rxoff;
        if (n > len) n = len;
        memcpy(buf, c->rxbuf + c->rxoff, n);
        c->rxoff += n;
        return (int)n;
    }
    for (;;) {
        if (c->ku_reply_pending && c->wpend_len == 0) {
            int kq = rh_queue_key_update_reply(c);
            if (kq == TRANSPORT_ERR) return TRANSPORT_ERR;
            if (kq == 0) {
                int fr = flush_wpend(c);
                if (fr == TRANSPORT_ERR) return TRANSPORT_ERR;
                if (fr == TRANSPORT_WANT_WRITE) return TRANSPORT_WANT_WRITE;
            }
        } else if (c->wpend_len) {
            int fr = flush_wpend(c);
            if (fr == TRANSPORT_ERR) return TRANSPORT_ERR;
            if (fr == TRANSPORT_WANT_WRITE) return TRANSPORT_WANT_WRITE;
        }

        uint8_t rec[MAX_RECORD], type; size_t rlen = 0;
        int rr = nb_next_record(c, &type, rec, sizeof rec, &rlen);
        if (rr != 0) return rr;
        if (type == CT_CCS) continue;
        if (type == CT_ALERT) return TRANSPORT_EOF;
        if (type != CT_APPDATA) return TRANSPORT_ERR;

/* use the consumed wire header as aead aad */
        uint8_t full[5 + MAX_RECORD];
        full[0] = type;
/* keep aad aligned with the consumed tls 1.3 record header */
        full[1] = 0x03; full[2] = 0x03;
        full[3] = (uint8_t)(rlen >> 8); full[4] = (uint8_t)(rlen & 0xff);
        memcpy(full + 5, rec, rlen);
        size_t plen = 0; uint8_t inner = 0;
        if (tls13_record_open_suite(c->aead, c->s_app_key, c->s_app_iv, c->s_app_seq,
                              full, 5 + rlen, c->rxbuf, sizeof c->rxbuf,
                              &plen, &inner) != TLS13_REC_OK) {
/* accept bare origin bytes only after authenticated application traffic */
            if (c->s_app_seq > 0)
                return rh_enter_read_plain(c, type, rec, rlen, buf, len);
            fprintf(stderr, "senkod: reality aead open fail seq=%llu rlen=%zu\n",
                    (unsigned long long)c->s_app_seq, rlen);
            return TRANSPORT_ERR;
        }
        c->s_app_seq++;
        if (inner == CT_ALERT) return TRANSPORT_EOF;
        if (inner == CT_HANDSHAKE) {
            if (rh_handle_post_hs(c, c->rxbuf, plen) != 0)
                return TRANSPORT_ERR;
            continue;
        }
        if (inner != CT_APPDATA) continue;
        c->rxlen = plen; c->rxoff = 0;
        if (plen == 0) continue;
        size_t n = plen < len ? plen : len;
        memcpy(buf, c->rxbuf, n);
        c->rxoff = n;
        return (int)n;
    }
}

static int rh_write(void *handle, const uint8_t *buf, size_t len) {
    rh_conn_t *c = (rh_conn_t *)handle;
    if (c->write_plain)
        return rh_write_plain(c, buf, len);
    if (c->wpend_len) {
        int fr = flush_wpend(c);
        if (fr != 0) return fr;
    }
    if (c->ku_reply_pending) {
        int kq = rh_queue_key_update_reply(c);
        if (kq != 0) return kq;
        int fr = flush_wpend(c);
        if (fr != 0) return fr;
    }
    if (len == 0) return 0;

/* small records until enough traffic; then full-size (utls-style) */
    size_t cap = (c->c_app_bytes < (uint64_t)RH_APP_BULK_AFTER)
                 ? (size_t)RH_APP_PLAIN_CAP : (size_t)RH_APP_BULK_CAP;
    if (len > cap) len = cap;
    size_t rlen = 0;
    if (tls13_record_seal_suite(c->aead, c->c_app_key, c->c_app_iv, c->c_app_seq,
                                CT_APPDATA, buf, len,
                                c->wpend, sizeof c->wpend, &rlen) != TLS13_REC_OK)
        return TRANSPORT_ERR;
    senko_trace_th(c, "tx_record", "reality app record");
    c->c_app_seq++;
    c->c_app_bytes += (uint64_t)len;
    c->wpend_len = rlen;
    c->wpend_off = 0;
    int fr = flush_wpend(c);
    if (fr == TRANSPORT_ERR) return TRANSPORT_ERR;
    return (int)len;
}

/* use zero-length writes to switch vision into bare socket mode */
static int rh_raw_write(void *handle, const uint8_t *buf, size_t len) {
    rh_conn_t *c = (rh_conn_t *)handle;
    if (!c) return TRANSPORT_ERR;
    c->write_plain = 1;
    if (len == 0) return 0;
    return rh_write_plain(c, buf, len);
}

static void rh_close(void *handle);

/* request pollout while a sealed record is still draining */
static int rh_want_write(void *handle) {
    rh_conn_t *c = (rh_conn_t *)handle;
    if (!c) return 0;
    return c->wpend_len > c->wpend_off;
}

static void *rh_open(int fd, const transport_tls_cfg_t *cfg) {
    if (fd < 0 || !cfg) return NULL;
    if (!cfg->reality_pbk || !cfg->reality_pbk[0]) return NULL;

    rh_params_t p;
    memset(&p, 0, sizeof p);
    p.sni = cfg->sni;

    size_t pbk_len = 0;
    if (b64_decode(cfg->reality_pbk, strlen(cfg->reality_pbk), p.pbk, sizeof p.pbk, &pbk_len) != 0 || pbk_len != 32) {
        return NULL;
    }

    if (cfg->reality_sid && cfg->reality_sid[0]) {
        size_t sid_hex_len = strlen(cfg->reality_sid);
        if (sid_hex_len > 16 || sid_hex_len % 2 != 0) return NULL;
        p.short_id_len = sid_hex_len / 2;
        for (size_t i = 0; i < p.short_id_len; ++i) {
            unsigned int val = 0;
            if (sscanf(cfg->reality_sid + 2 * i, "%2x", &val) != 1) return NULL;
            p.short_id[i] = (uint8_t)val;
        }
    } else {
        p.short_id_len = 0;
    }

    p.version[0] = 1;
    p.version[1] = 8;
    p.version[2] = 0;

/* map the fingerprint and generate firefox's decoy point in crypto code */
    p.fp = TLS_FP_CHROME;
    if (cfg->fingerprint && cfg->fingerprint[0]) {
        if      (strcmp(cfg->fingerprint, "chrome") == 0 ||
                 strcmp(cfg->fingerprint, "chrome_auto") == 0)
            p.fp = TLS_FP_CHROME;
        else if (strcmp(cfg->fingerprint, "firefox") == 0) p.fp = TLS_FP_FIREFOX;
        else if (strcmp(cfg->fingerprint, "edge") == 0)    p.fp = TLS_FP_EDGE;
        else if (strcmp(cfg->fingerprint, "qq") == 0)      p.fp = TLS_FP_QQ;
        else if (strcmp(cfg->fingerprint, "random") == 0 ||
                 strcmp(cfg->fingerprint, "randomized") == 0) p.fp = TLS_FP_RANDOMIZED;
        else {
/* silent fallback hid broken fp= values; log once per open */
            fprintf(stderr, "senkod: unknown fp=%s, using chrome\n",
                    cfg->fingerprint);
            p.fp = TLS_FP_CHROME;
        }
    }
    if (p.fp == TLS_FP_FIREFOX) {
        EC_GROUP *grp = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
        EC_POINT *pt = grp ? EC_POINT_new(grp) : NULL;
        BIGNUM *ord = grp ? BN_new() : NULL;
        BIGNUM *scalar = grp ? BN_new() : NULL;
        BN_CTX *bnctx = grp ? BN_CTX_new() : NULL;
        if (grp && pt && ord && scalar && bnctx &&
            EC_GROUP_get_order(grp, ord, bnctx) == 1 &&
            BN_rand_range(scalar, ord) == 1 &&
            !BN_is_zero(scalar) &&
            EC_POINT_mul(grp, pt, scalar, NULL, NULL, bnctx) == 1 &&
            EC_POINT_point2oct(grp, pt, POINT_CONVERSION_UNCOMPRESSED,
                               p.p256_pub, sizeof p.p256_pub, bnctx) == 65) {
            p.has_p256 = 1;
        }
        if (bnctx) BN_CTX_free(bnctx);
        if (scalar) BN_free(scalar);
        if (ord) BN_free(ord);
        if (pt) EC_POINT_free(pt);
        if (grp) EC_GROUP_free(grp);
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return NULL;
    if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) return NULL;

/* short handshake budget: free nodes fail fast (proto) */
    struct timeval tv;
    tv.tv_sec = 4;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    errno = 0;
    int pr = poll(&pfd, 1, 4000);
    if (pr <= 0) {
        if (pr == 0)
            fprintf(stderr, "senkod: reality tcp connect timed out\n");
        else
            fprintf(stderr, "senkod: reality tcp connect wait failed: errno=%d\n",
                    errno);
        fcntl(fd, F_SETFL, flags);
        return NULL;
    }

    int err_code = 0;
    socklen_t err_len = sizeof err_code;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err_code, &err_len) < 0 || err_code != 0) {
        int saved_errno = errno;
        fprintf(stderr, "senkod: reality tcp connect failed: so_error=%d errno=%d\n",
                err_code, saved_errno);
        fcntl(fd, F_SETFL, flags);
        return NULL;
    }

    rh_status_t err = RH_OK;
    void *h = reality_handshake_open(fd, &p, &err);
    if (!h) {
        int saved_errno = errno;
        fprintf(stderr, "senkod: reality handshake failed: %s (%d), errno=%d\n",
                rh_status_name(err), (int)err, saved_errno);
    }

    if (fcntl(fd, F_SETFL, flags) < 0) {
        if (h) rh_close(h);
        return NULL;
    }

    return h;
}

static void rh_close(void *handle) {
    if (handle) free(handle);
}

const transport_vt_t transport_reality = {
    rh_open, rh_read, rh_write, rh_raw_write, rh_close, rh_want_write
};
