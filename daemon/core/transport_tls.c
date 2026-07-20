#include "transport.h"

#include <string.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#define SENKO_CA_BUNDLE "/usr/lib/senkotlsfix/cacert.pem"

typedef struct {
    SSL_CTX *ctx;
    SSL     *ssl;
    int      fd;
    int      raw_rx;
    int      raw_tx;
} tls_handle_t;

/* keep ctx per conn so lifetime stays simple while the stack settles */
static void *tls_open(int fd, const transport_tls_cfg_t *cfg) {
    if (fd < 0) return NULL;

    tls_handle_t *h = (tls_handle_t *)OPENSSL_zalloc(sizeof *h);
    if (!h) return NULL;

    h->ctx = SSL_CTX_new(TLS_client_method());
    if (!h->ctx) { OPENSSL_free(h); return NULL; }

/* pin the tls range so old devices stay inside support */
    SSL_CTX_set_min_proto_version(h->ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(h->ctx, TLS1_3_VERSION);

    int reality = (cfg && cfg->reality_pbk && cfg->reality_pbk[0]);
    if (reality) {
        SSL_CTX_set_verify(h->ctx, SSL_VERIFY_NONE, NULL);
    } else {
        SSL_CTX_set_verify(h->ctx, SSL_VERIFY_PEER, NULL);
        SSL_CTX_set_default_verify_paths(h->ctx);
        if (access(SENKO_CA_BUNDLE, R_OK) == 0)
            SSL_CTX_load_verify_locations(h->ctx, SENKO_CA_BUNDLE, NULL);
    }

    h->ssl = SSL_new(h->ctx);
    if (!h->ssl) { SSL_CTX_free(h->ctx); OPENSSL_free(h); return NULL; }

    if (SSL_set_fd(h->ssl, fd) != 1) {
        SSL_free(h->ssl); SSL_CTX_free(h->ctx); OPENSSL_free(h);
        return NULL;
    }
    h->fd = fd;

/* use the configured server name so cert checks and fronting stay aligned */
    if (cfg && cfg->sni && cfg->sni[0]) {
        SSL_set_tlsext_host_name(h->ssl, cfg->sni);
        SSL_set1_host(h->ssl, cfg->sni);
    }

    SSL_set_connect_state(h->ssl); /* client mode */
    return h;
}

static int ssl_err_to_transport(SSL *ssl, int ret) {
    int e = SSL_get_error(ssl, ret);
    switch (e) {
        case SSL_ERROR_WANT_READ:  return TRANSPORT_WANT_READ;
        case SSL_ERROR_WANT_WRITE: return TRANSPORT_WANT_WRITE;
        case SSL_ERROR_ZERO_RETURN: return TRANSPORT_EOF; /* peer closed cleanly */
        case SSL_ERROR_SYSCALL:
/* peer reset without close notify, treat it as eof and drain */
            return TRANSPORT_EOF;
        default:
            return TRANSPORT_ERR;
    }
}

static int tls_read(void *handle, uint8_t *buf, size_t len) {
    tls_handle_t *h = (tls_handle_t *)handle;
    if (h->raw_rx) {
        ssize_t n = read(h->fd, buf, len);
        if (n > 0) return (int)n;
        if (n == 0) return TRANSPORT_EOF;
        return TRANSPORT_WANT_READ;
    }
    int n = SSL_read(h->ssl, buf, (int)len);
    if (n > 0) return n;
    return ssl_err_to_transport(h->ssl, n);
}

static int tls_write(void *handle, const uint8_t *buf, size_t len) {
    tls_handle_t *h = (tls_handle_t *)handle;
    if (h->raw_tx) {
        ssize_t n = write(h->fd, buf, len);
        if (n > 0) return (int)n;
        if (n == 0) return TRANSPORT_WANT_WRITE;
        return TRANSPORT_WANT_WRITE;
    }
    int n = SSL_write(h->ssl, buf, (int)len);
    if (n > 0) return n;
    return ssl_err_to_transport(h->ssl, n);
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
        SSL_free(h->ssl);
    }
    if (h->ctx) SSL_CTX_free(h->ctx);
    OPENSSL_free(h);
}

const transport_vt_t transport_tls = {
    tls_open, tls_read, tls_write, tls_raw_write, tls_close, NULL
};

/* reality uses its own handshake path, not ssl_connect */
