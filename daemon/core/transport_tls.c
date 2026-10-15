#include "transport.h"

#include <errno.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include "../../common/senko_paths.h"

#define SENKO_CA_BUNDLE SENKO_USR_LIB "/senko/cacert.pem"

typedef struct {
    SSL_CTX *ctx;
    SSL     *ssl;
    int      fd;
    int      raw_rx;
    int      raw_tx;
    /* where the incoming byte stream stands in tls record framing */
    uint8_t  rec_header[5];
    size_t   rec_header_len;
    size_t   rec_body_left;
    int      established; /* a record crossed after the handshake */
    net_fail_t *fail;
} tls_handle_t;

/* follows record boundaries in the raw bytes openssl reads from the socket */
static long record_tracker(BIO *bio, int oper, const char *argp, size_t len,
                           int argi, long argl, int ret, size_t *processed) {
    (void)len; (void)argi; (void)argl;
    tls_handle_t *h = (tls_handle_t *)BIO_get_callback_arg(bio);
    if (!h || oper != (BIO_CB_READ | BIO_CB_RETURN) || ret <= 0 || !processed || !argp)
        return ret;
    const uint8_t *p = (const uint8_t *)argp;
    size_t n = *processed;
    while (n > 0) {
        if (h->rec_body_left > 0) {
            size_t take = n < h->rec_body_left ? n : h->rec_body_left;
            h->rec_body_left -= take;
            p += take;
            n -= take;
            continue;
        }
        h->rec_header[h->rec_header_len++] = *p++;
        --n;
        if (h->rec_header_len == sizeof h->rec_header) {
            h->rec_body_left = ((size_t)h->rec_header[3] << 8) | h->rec_header[4];
            h->rec_header_len = 0;
        }
    }
    return ret;
}

/* go's tls, which xray servers and their clients use, ends a stream quietly
   when the socket closes between records; cdn fronts close that way without
   close_notify. a close inside a record is still a cut stream */
static int closed_between_records(const tls_handle_t *h) {
    return h->rec_header_len == 0 && h->rec_body_left == 0;
}

/* the fatal alert openssl tries to send after the eof fails with EPIPE on a
   closed socket, and that lands on top of the queue, so look through it */
static int queue_has_unexpected_eof(void) {
#ifdef SSL_R_UNEXPECTED_EOF_WHILE_READING
    unsigned long code;
    while ((code = ERR_get_error()) != 0)
        if (ERR_GET_LIB(code) == ERR_LIB_SSL &&
            ERR_GET_REASON(code) == SSL_R_UNEXPECTED_EOF_WHILE_READING)
            return 1;
#endif
    return 0;
}

struct transport_tls_shared {
    SSL_CTX *ctx;
};

static SSL_CTX *tls_context_new(const transport_tls_cfg_t *cfg) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return NULL;

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);

    /* h2 alpn would send binary frames into the http/1 handlers */
    static const unsigned char alpn[] = "\x08http/1.1";
    SSL_CTX_set_alpn_protos(ctx, alpn, sizeof alpn - 1);
    SSL_CTX_set_ciphersuites(ctx,
        "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");
    SSL_CTX_set1_groups_list(ctx, "X25519:P-256:P-384");

    int reality = (cfg && cfg->reality_pbk && cfg->reality_pbk[0]);
    if (reality || (cfg && cfg->insecure)) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
#if defined(__APPLE__)
        /* ios can report unregistered scheme for openssl's default store,
           and ios 12+ removes the injected tlsfix roots */
        if (SSL_CTX_load_verify_locations(ctx, SENKO_CA_BUNDLE, NULL) != 1) {
            fprintf(stderr, "senkod: tls roots unavailable at %s\n", SENKO_CA_BUNDLE);
            SSL_CTX_free(ctx);
            return NULL;
        }
#else
        if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
            SSL_CTX_free(ctx);
            return NULL;
        }
#endif
    }
    return ctx;
}

transport_tls_shared_t *transport_tls_shared_create(const transport_tls_cfg_t *cfg) {
    transport_tls_shared_t *shared = OPENSSL_zalloc(sizeof *shared);
    if (!shared) return NULL;
    shared->ctx = tls_context_new(cfg);
    if (!shared->ctx) {
        OPENSSL_free(shared);
        return NULL;
    }
    return shared;
}

void transport_tls_shared_destroy(transport_tls_shared_t *shared) {
    if (!shared) return;
    SSL_CTX_free(shared->ctx);
    OPENSSL_free(shared);
}

static void *tls_open(int fd, const transport_tls_cfg_t *cfg) {
    if (fd < 0) return NULL;

    tls_handle_t *h = (tls_handle_t *)OPENSSL_zalloc(sizeof *h);
    if (!h) return NULL;

    if (cfg && cfg->shared_ctx) {
        h->ctx = cfg->shared_ctx->ctx;
        if (SSL_CTX_up_ref(h->ctx) != 1) h->ctx = NULL;
    } else {
        h->ctx = tls_context_new(cfg);
    }
    if (!h->ctx) { OPENSSL_free(h); return NULL; }

    h->ssl = SSL_new(h->ctx);
    if (!h->ssl) { SSL_CTX_free(h->ctx); OPENSSL_free(h); return NULL; }

    if (SSL_set_fd(h->ssl, fd) != 1) {
        SSL_free(h->ssl); SSL_CTX_free(h->ctx); OPENSSL_free(h);
        return NULL;
    }
    h->fd = fd;
    h->fail = cfg ? cfg->fail : NULL;
    BIO *rbio = SSL_get_rbio(h->ssl);
    if (rbio) {
        BIO_set_callback_arg(rbio, (char *)h);
        BIO_set_callback_ex(rbio, record_tracker);
    }

    const char *host = NULL;
    if (cfg) host = cfg->sni && cfg->sni[0] ? cfg->sni : cfg->peer_host;
    int verify = !(cfg && (cfg->insecure ||
                  (cfg->reality_pbk && cfg->reality_pbk[0])));
    if (verify && (!host || !host[0])) {
        SSL_free(h->ssl); SSL_CTX_free(h->ctx); OPENSSL_free(h);
        return NULL;
    }
    if (host && host[0]) {
        uint8_t ip[16];
        int literal = inet_pton(AF_INET, host, ip) == 1 ||
                      inet_pton(AF_INET6, host, ip) == 1;
        if ((!literal && SSL_set_tlsext_host_name(h->ssl, host) != 1) ||
            (verify && (literal
                ? X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(h->ssl), host) != 1
                : SSL_set1_host(h->ssl, host) != 1))) {
            SSL_free(h->ssl); SSL_CTX_free(h->ctx); OPENSSL_free(h);
            return NULL;
        }
    }

    SSL_set_connect_state(h->ssl); /* client mode */
    return h;
}

/* what a failed handshake says about the server, for the probe's diagnosis */
static void note_tls_failure(const tls_handle_t *h, int ssl_error, unsigned long code,
                             int saved_errno) {
    if (!h->fail || h->established) return;
    int reason = code && ERR_GET_LIB(code) == ERR_LIB_SSL ? ERR_GET_REASON(code) : 0;
    if (reason == SSL_R_CERTIFICATE_VERIFY_FAILED) {
        switch (SSL_get_verify_result(h->ssl)) {
        case X509_V_ERR_CERT_NOT_YET_VALID:
            net_fail_note(h->fail, NET_FAIL_CERT_NOT_YET_VALID, 0);
            return;
        case X509_V_ERR_CERT_HAS_EXPIRED:
            net_fail_note(h->fail, NET_FAIL_CERT_EXPIRED, 0);
            return;
        case X509_V_ERR_HOSTNAME_MISMATCH:
        case X509_V_ERR_IP_ADDRESS_MISMATCH:
            net_fail_note(h->fail, NET_FAIL_CERT_NAME, 0);
            return;
        default:
            net_fail_note(h->fail, NET_FAIL_CERT_UNTRUSTED, 0);
            return;
        }
    }
    /* a plain http or proxy port answers the client hello with text */
    if (reason == SSL_R_WRONG_VERSION_NUMBER || reason == SSL_R_PACKET_LENGTH_TOO_LONG) {
        net_fail_note(h->fail, NET_FAIL_NOT_TLS, 0);
        return;
    }
    if (saved_errno == ECONNRESET) {
        net_fail_note(h->fail, NET_FAIL_RESET, 0);
        return;
    }
    if (saved_errno == ETIMEDOUT) {
        net_fail_note(h->fail, NET_FAIL_TIMEOUT, 0);
        return;
    }
#ifdef SSL_R_UNEXPECTED_EOF_WHILE_READING
    if (reason == SSL_R_UNEXPECTED_EOF_WHILE_READING) {
        net_fail_note(h->fail, NET_FAIL_CLOSED, 0);
        return;
    }
#endif
    if (ssl_error == SSL_ERROR_SYSCALL && saved_errno == 0) {
        net_fail_note(h->fail, NET_FAIL_CLOSED, 0);
        return;
    }
    net_fail_note(h->fail, NET_FAIL_TLS, reason);
}

static int ssl_err_to_transport(const tls_handle_t *h, int ret) {
    int saved_errno = errno;
    int e = SSL_get_error(h->ssl, ret);
    switch (e) {
        case SSL_ERROR_WANT_READ:  return TRANSPORT_WANT_READ;
        case SSL_ERROR_WANT_WRITE: return TRANSPORT_WANT_WRITE;
        case SSL_ERROR_ZERO_RETURN:
            if (h->fail && !h->established) net_fail_note(h->fail, NET_FAIL_CLOSED, 0);
            return TRANSPORT_EOF; /* peer closed cleanly */
        default: {
            unsigned long code = ERR_peek_error();
            if (e == SSL_ERROR_SSL && h->established && closed_between_records(h) &&
                queue_has_unexpected_eof())
                return TRANSPORT_EOF;
            const char *reason = code ? ERR_reason_error_string(code) : NULL;
            note_tls_failure(h, e, code, saved_errno);
            fprintf(stderr, "senkod: tls stream failed: %s (ssl error %d, errno %d %s)\n",
                    reason ? reason : e == SSL_ERROR_SYSCALL ? "socket error" : "no reason",
                    e, saved_errno, saved_errno ? strerror(saved_errno) : "none");
            ERR_clear_error();
            return TRANSPORT_ERR;
        }
    }
}

static int tls_read(void *handle, uint8_t *buf, size_t len) {
    tls_handle_t *h = (tls_handle_t *)handle;
    if (h->raw_rx) {
        ssize_t n = read(h->fd, buf, len);
        if (n > 0) return (int)n;
        if (n == 0) return TRANSPORT_EOF;
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return TRANSPORT_WANT_READ;
        return TRANSPORT_ERR;
    }
    /* SSL_get_error trusts the thread's error queue; a stale entry from
       another connection turned a plain EAGAIN here into a fatal error */
    ERR_clear_error();
    int n = SSL_read(h->ssl, buf, (int)len);
    if (n > 0) {
        h->established = 1;
        return n;
    }
    return ssl_err_to_transport(h, n);
}

static int tls_write(void *handle, const uint8_t *buf, size_t len) {
    tls_handle_t *h = (tls_handle_t *)handle;
    if (h->raw_tx) {
        ssize_t n = write(h->fd, buf, len);
        if (n > 0) return (int)n;
        if (n == 0) return TRANSPORT_WANT_WRITE;
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return TRANSPORT_WANT_WRITE;
        return TRANSPORT_ERR;
    }
    ERR_clear_error();
    int n = SSL_write(h->ssl, buf, (int)len);
    if (n > 0) {
        h->established = 1;
        return n;
    }
    return ssl_err_to_transport(h, n);
}

static int tls_raw_write(void *handle, const uint8_t *buf, size_t len) {
    tls_handle_t *h = (tls_handle_t *)handle;
    if (len == 0) {
        h->raw_rx = 1;
        h->raw_tx = 1;
        return 0;
    }
    h->raw_tx = 1;
    return tls_write(handle, buf, len);
}

static void tls_close(void *handle) {
    tls_handle_t *h = (tls_handle_t *)handle;
    if (!h) return;
    if (h->ssl) {
        SSL_shutdown(h->ssl);
        /* a close during the handshake queues "shutdown while in init" on
           this thread, where the next connection's read would find it */
        ERR_clear_error();
        SSL_free(h->ssl);
    }
    if (h->ctx) SSL_CTX_free(h->ctx);
    OPENSSL_free(h);
}

const transport_vt_t transport_tls = {
    tls_open, tls_read, tls_write, tls_raw_write, tls_close, NULL
};

/* reality uses its own handshake path, not ssl_connect */
