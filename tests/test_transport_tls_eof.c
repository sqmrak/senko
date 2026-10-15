/* how the tls transport ends a stream: a close after the last full record is
   a clean eof, as cdn fronts do it without close_notify; a cut inside a record
   is still an error, so a truncated stream never looks complete */
#define _DEFAULT_SOURCE

#include "transport.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

static int failures;

static void check(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

typedef enum { END_CLOSE_NOTIFY, END_TCP_AFTER_RECORD, END_CUT_IN_RECORD, END_HOLD } end_t;

typedef struct {
    int fd;
    end_t end;
    SSL_CTX *ctx;
} server_t;

static SSL_CTX *server_ctx(void) {
    EVP_PKEY *key = EVP_EC_gen("P-256");
    X509 *cert = X509_new();
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!key || !cert || !ctx) return NULL;
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
    X509_set_pubkey(cert, key);
    X509_NAME *name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               (const unsigned char *)"eof.test", -1, -1, 0);
    X509_set_issuer_name(cert, name);
    X509_sign(cert, key, EVP_sha256());
    SSL_CTX_use_certificate(ctx, cert);
    SSL_CTX_use_PrivateKey(ctx, key);
    X509_free(cert);
    EVP_PKEY_free(key);
    return ctx;
}

static void *serve(void *arg) {
    server_t *server = arg;
    SSL *ssl = SSL_new(server->ctx);
    SSL_set_fd(ssl, server->fd);
    if (SSL_accept(ssl) == 1) {
        /* take in the client's tls 1.3 Finished first, as a real server would
           before it ends the stream; a short timed read processes it */
        struct timeval wait = { 0, 300000 };
        setsockopt(server->fd, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
        char none;
        (void)SSL_read(ssl, &none, 1);
        if (server->end == END_HOLD) {
            /* a live connection with nothing to say for a while */
            struct timeval hold = { 1, 0 };
            setsockopt(server->fd, SOL_SOCKET, SO_RCVTIMEO, &hold, sizeof hold);
            (void)SSL_read(ssl, &none, 1);
            close(server->fd);
            SSL_free(ssl);
            return NULL;
        }
        static const char body[] = "the whole answer";
        if (server->end == END_CUT_IN_RECORD) {
            /* the record goes to memory, and only half of it reaches the wire */
            BIO *record = BIO_new(BIO_s_mem());
            SSL_set0_wbio(ssl, record);
            SSL_write(ssl, body, (int)sizeof body - 1);
            char *bytes = NULL;
            long len = BIO_get_mem_data(record, &bytes);
            if (len > 2) (void)write(server->fd, bytes, (size_t)len / 2);
        } else {
            SSL_write(ssl, body, (int)sizeof body - 1);
            if (server->end == END_CLOSE_NOTIFY) SSL_shutdown(ssl);
        }
    }
    close(server->fd);
    SSL_free(ssl);
    return NULL;
}

/* reads until the stream ends; returns the final code, the bytes in *got */
static int drain(int fd, void *handle, char *buf, size_t cap, size_t *got) {
    *got = 0;
    for (int round = 0; round < 400; ++round) {
        int r = transport_tls.read(handle, (uint8_t *)buf + *got, cap - *got);
        if (r > 0) { *got += (size_t)r; continue; }
        if (r == TRANSPORT_WANT_READ || r == TRANSPORT_WANT_WRITE) {
            struct pollfd pfd = { fd, r == TRANSPORT_WANT_READ ? POLLIN : POLLOUT, 0 };
            (void)poll(&pfd, 1, 50);
            continue;
        }
        return r;
    }
    return 0;
}

static int run(SSL_CTX *ctx, end_t end, char *buf, size_t cap, size_t *got) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return 0;
    int flags = fcntl(pair[0], F_GETFL, 0);
    fcntl(pair[0], F_SETFL, flags | O_NONBLOCK);
    server_t server = { pair[1], end, ctx };
    pthread_t thread;
    pthread_create(&thread, NULL, serve, &server);
    transport_tls_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.sni = "eof.test";
    cfg.insecure = end != END_HOLD;
    net_fail_t fail = {0};
    cfg.fail = &fail;
    void *handle = transport_tls.open(pair[0], &cfg);
    int result = handle ? drain(pair[0], handle, buf, cap, got) : 0;
    if (end == END_HOLD)
        check("untrusted certificate keeps its precise reason", result == TRANSPORT_ERR &&
              fail.kind == NET_FAIL_CERT_UNTRUSTED);
    if (handle) transport_tls.close(handle);
    pthread_join(thread, NULL);
    close(pair[0]);
    return result;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN); /* senkod ignores it; a closing peer must not kill the test */
    SSL_CTX *ctx = server_ctx();
    check("test server context", ctx != NULL);
    if (!ctx) return 1;
    char buf[128];
    size_t got = 0;

    int r = run(ctx, END_CLOSE_NOTIFY, buf, sizeof buf, &got);
    check("close_notify ends the stream cleanly",
          r == TRANSPORT_EOF && got == 16 && memcmp(buf, "the whole answer", 16) == 0);

    r = run(ctx, END_TCP_AFTER_RECORD, buf, sizeof buf, &got);
    check("a tcp close after the last record is a clean end",
          r == TRANSPORT_EOF && got == 16 && memcmp(buf, "the whole answer", 16) == 0);

    r = run(ctx, END_CUT_IN_RECORD, buf, sizeof buf, &got);
    check("a cut inside a record is an error", r == TRANSPORT_ERR && got == 0);

    run(ctx, END_HOLD, buf, sizeof buf, &got);

    /* a plaintext port must be diagnosed separately from certificate failures */
    {
        int pair[2];
        check("plain socketpair", socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
        fcntl(pair[0], F_SETFL, fcntl(pair[0], F_GETFL, 0) | O_NONBLOCK);
        transport_tls_cfg_t cfg = {0};
        net_fail_t fail = {0};
        cfg.fail = &fail;
        cfg.sni = "plain.test";
        void *handle = transport_tls.open(pair[0], &cfg);
        const char answer[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        check("plain answer", write(pair[1], answer, sizeof answer - 1) == sizeof answer - 1);
        int result = handle ? drain(pair[0], handle, buf, sizeof buf, &got) : 0;
        check("plain port identified", result == TRANSPORT_ERR && fail.kind == NET_FAIL_NOT_TLS);
        if (handle) transport_tls.close(handle);
        close(pair[0]); close(pair[1]);
    }

    /* a handshake cut short by close leaves "shutdown while in init" on the
       thread's error queue; the next idle read elsewhere must still just wait */
    {
        int live[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, live);
        fcntl(live[0], F_SETFL, fcntl(live[0], F_GETFL, 0) | O_NONBLOCK);
        server_t holder = { live[1], END_HOLD, ctx };
        pthread_t thread;
        pthread_create(&thread, NULL, serve, &holder);
        transport_tls_cfg_t cfg;
        memset(&cfg, 0, sizeof cfg);
        cfg.sni = "eof.test";
        cfg.insecure = 1;
        void *idle = transport_tls.open(live[0], &cfg);
        uint8_t byte;
        for (int i = 0; idle && i < 20; ++i) {
            (void)transport_tls.read(idle, &byte, 1);
            struct pollfd pfd = { live[0], POLLIN, 0 };
            (void)poll(&pfd, 1, 10);
        }
        int dead[2];
        socketpair(AF_UNIX, SOCK_STREAM, 0, dead);
        fcntl(dead[0], F_SETFL, fcntl(dead[0], F_GETFL, 0) | O_NONBLOCK);
        void *cut = transport_tls.open(dead[0], &cfg);
        if (cut) {
            (void)transport_tls.read(cut, &byte, 1); /* sends the ClientHello */
            transport_tls.close(cut);
        }
        close(dead[0]);
        close(dead[1]);
        check("another connection's aborted handshake does not break an idle read",
              idle && transport_tls.read(idle, &byte, 1) == TRANSPORT_WANT_READ);
        if (idle) transport_tls.close(idle);
        pthread_join(thread, NULL);
        close(live[0]);
    }

    SSL_CTX_free(ctx);
    if (failures) {
        fprintf(stderr, "%d tls eof check(s) failed\n", failures);
        return 1;
    }
    puts("all tls eof checks passed");
    return 0;
}
