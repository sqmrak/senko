#define _DEFAULT_SOURCE

#include "hy2_engine.h"
#include "core/hy2_proto.h"
#include "core/senko_time.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>

#include <ngtcp2/ngtcp2.h>
/* ngtcp2_crypto.h repeats a typedef, which c99 only accepts from c11 on */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtypedef-redefinition"
#endif
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include <nghttp3/nghttp3.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#define HY2_STREAMS_MAX 64
#define HY2_PENDING_MAX 32
/* the rings are the flow control windows, so they never overflow */
#define HY2_RING_SIZE (128 * 1024)
#define HY2_CONN_WINDOW (1024 * 1024)
#define HY2_UDP_PACKET_MAX 2048
#define HY2_READ_BURST 32
#define HY2_WRITE_BURST 64
#define HY2_IDLE_TIMEOUT_MS 30000
/* a third of the idle timeout, so one lost ping does not end a quiet flow */
#define HY2_KEEPALIVE_MS 10000
#define HY2_RETRY_COOLDOWN_MS 1500
#define HY2_POLL_MAX_MS 1000
/* an icmp refusal only counts once this long passed without any valid packet */
#define HY2_REFUSED_GRACE_MS 2000
#define HY2_AUTH_HEADER "hysteria-auth"
#define HY2_DETAIL_MAX 200
/* the detail plus the server address and port it is about */
#define HY2_ERROR_MAX (HY2_DETAIL_MAX + 96)

typedef enum {
    HY2_STATE_IDLE = 0, /* no connection, the next dial starts one */
    HY2_STATE_CONNECTING,
    HY2_STATE_READY,
    HY2_STATE_FAILED    /* every address failed; dials refuse until the cooldown */
} hy2_state_t;

/* the half of a flow that the transport reads. it outlives the engine's stream
   so the tcp bridge can still ask why a descriptor ended */
typedef struct {
    int refs;
    volatile int aborted;
} hy2_endpoint_t;

typedef struct {
    int in_use;
    int fd;
    int64_t id;
    hy2_endpoint_t *endpoint;
    uint8_t *send_ring;
    uint8_t *recv_ring;
    uint64_t send_put;   /* bytes read from the app */
    uint64_t send_sent;  /* bytes handed to quic */
    uint64_t send_acked; /* bytes quic no longer needs */
    uint64_t recv_in;
    uint64_t recv_out;
    int local_eof;       /* the app finished sending */
    int fin_sent;
    int send_dead;       /* the server stopped reading, or the stream is gone */
    int blocked;         /* the stream's flow control window is full */
    int remote_fin;
    int wr_shut;         /* the app was told eof */
    int quic_closed;
    int reset_by_peer;
    int app_gone;
    unsigned skip_gen;
} hy2_stream_t;

typedef struct {
    int fd;
    hy2_endpoint_t *endpoint;
} hy2_pending_t;

struct hy2_engine {
    hy2_engine_config_t config;
    uint8_t addresses[16][16];
    uint8_t address_lens[16];
    char sni[256];
    char peer_host[256];
    char auth[256];
    char obfs[HY2_SALAMANDER_KEY_MAX + 1];
    uint8_t pin[32];
    int has_pin;
    int attempt_ms;

    pthread_t thread;
    int thread_started;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    int wake[2];

    /* guarded by lock */
    hy2_state_t state;
    int want_connect;
    int stop;
    char last_error[HY2_ERROR_MAX];
    int64_t retry_at_ms;
    hy2_pending_t pending[HY2_PENDING_MAX];
    size_t pending_count;

    /* engine thread only from here on */
    SSL_CTX *ssl_ctx;
    size_t cursor;          /* next address to try */
    size_t tries_left;
    int attempting;         /* a connect request is working through its addresses */
    char detail[HY2_DETAIL_MAX];
    char tls_detail[HY2_DETAIL_MAX];

    int udp_fd;
    struct sockaddr_storage local;
    socklen_t local_len;
    struct sockaddr_storage remote;
    socklen_t remote_len;
    ngtcp2_path path;
    ngtcp2_conn *conn;
    SSL *ssl;
    ngtcp2_crypto_ossl_ctx *octx;
    ngtcp2_crypto_conn_ref conn_ref;
    nghttp3_conn *h3;
    int64_t auth_stream;
    int handshake_done;
    int auth_done;
    int64_t attempt_deadline_ms;
    hy2_stream_t streams[HY2_STREAMS_MAX];
    size_t round_robin;
    unsigned flush_gen;
    int more_to_send;
    int conn_dead;
    int refused;           /* the last recv failed with an icmp port unreachable */
    int64_t last_rx_ms;
};

static pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;
static hy2_endpoint_t *registry[FD_SETSIZE];

static void endpoint_release(hy2_endpoint_t *endpoint) {
    if (endpoint && __sync_sub_and_fetch(&endpoint->refs, 1) == 0) free(endpoint);
}

/* ---- the descriptor the tcp bridge sees ---------------------------------- */

typedef struct {
    int fd;
    hy2_endpoint_t *endpoint;
} hy2_handle_t;

static void *hy2_open(int fd, const transport_tls_cfg_t *cfg) {
    (void)cfg;
    if (fd < 0 || fd >= FD_SETSIZE) return NULL;
    pthread_mutex_lock(&registry_lock);
    hy2_endpoint_t *endpoint = registry[fd];
    registry[fd] = NULL;
    pthread_mutex_unlock(&registry_lock);
    if (!endpoint) return NULL;
    hy2_handle_t *handle = malloc(sizeof *handle);
    if (!handle) {
        endpoint_release(endpoint);
        return NULL;
    }
    handle->fd = fd;
    handle->endpoint = endpoint;
    return handle;
}

/* a closed descriptor reads as eof, and only the flag tells a stream that
   ended cleanly from one the engine cut off */
static int hy2_read(void *h, uint8_t *buf, size_t len) {
    hy2_handle_t *handle = h;
    ssize_t n = read(handle->fd, buf, len);
    if (n > 0) return (int)n;
    if (n == 0) return handle->endpoint->aborted ? TRANSPORT_ERR : TRANSPORT_EOF;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return TRANSPORT_WANT_READ;
    return TRANSPORT_ERR;
}

static int hy2_write(void *h, const uint8_t *buf, size_t len) {
    hy2_handle_t *handle = h;
    if (handle->endpoint->aborted) return TRANSPORT_ERR;
    ssize_t n = write(handle->fd, buf, len);
    if (n >= 0) return (int)n;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return TRANSPORT_WANT_WRITE;
    return TRANSPORT_ERR;
}

static void hy2_close(void *h) {
    hy2_handle_t *handle = h;
    if (!handle) return;
    endpoint_release(handle->endpoint);
    free(handle);
}

const transport_vt_t transport_hy2 = {
    hy2_open, hy2_read, hy2_write, hy2_write, hy2_close, NULL, NULL
};

/* ---- small helpers ------------------------------------------------------- */

static void detail_set(hy2_engine_t *e, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(e->detail, sizeof e->detail, format, args);
    va_end(args);
}

static ngtcp2_tstamp now_ts(void) {
    return (ngtcp2_tstamp)senko_now_ms() * NGTCP2_MILLISECONDS;
}

static void wake_thread(hy2_engine_t *e) {
    uint8_t byte = 1;
    /* a full pipe already holds a wakeup */
    ssize_t ignored = write(e->wake[1], &byte, 1);
    (void)ignored;
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) return -1;
    return fcntl(fd, F_SETFD, FD_CLOEXEC);
}

static int hex_digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* the pin is written as 64 hex digits, often with colons between the bytes */
static int pin_parse(const char *text, uint8_t out[32]) {
    size_t count = 0;
    int high = -1;
    for (const char *p = text; *p; ++p) {
        if (*p == ':' || *p == ' ') continue;
        int digit = hex_digit((unsigned char)*p);
        if (digit < 0) return -1;
        if (high < 0) {
            high = digit;
            continue;
        }
        if (count >= 32) return -1;
        out[count++] = (uint8_t)(high << 4 | digit);
        high = -1;
    }
    return (high < 0 && count == 32) ? 0 : -1;
}

static int is_ip_literal(const char *host) {
    unsigned char buffer[16];
    return inet_pton(AF_INET, host, buffer) == 1 || inet_pton(AF_INET6, host, buffer) == 1;
}

/* ---- streams ------------------------------------------------------------- */

static hy2_stream_t *stream_by_id(hy2_engine_t *e, int64_t id) {
    for (size_t i = 0; i < HY2_STREAMS_MAX; ++i)
        if (e->streams[i].in_use && e->streams[i].id == id) return &e->streams[i];
    return NULL;
}

static void ring_copy_in(uint8_t *ring, uint64_t position, const uint8_t *data, size_t len) {
    /* a bare fin arrives as a zero length chunk with a null pointer */
    if (len == 0) return;
    size_t start = (size_t)(position & (HY2_RING_SIZE - 1));
    size_t first = HY2_RING_SIZE - start;
    if (first > len) first = len;
    memcpy(ring + start, data, first);
    if (len > first) memcpy(ring, data + first, len - first);
}

static void stream_free(hy2_stream_t *s, int aborted) {
    if (aborted) s->endpoint->aborted = 1;
    __sync_synchronize();
    close(s->fd);
    endpoint_release(s->endpoint);
    free(s->send_ring);
    free(s->recv_ring);
    memset(s, 0, sizeof *s);
    s->fd = -1;
}

/* a stream goes once quic is done with it and the app has every byte */
static void stream_reap(hy2_stream_t *s) {
    if (!s->in_use || !s->quic_closed) return;
    if (!s->app_gone && s->recv_in != s->recv_out) return;
    stream_free(s, s->reset_by_peer);
}

static void stream_abort_app(hy2_engine_t *e, hy2_stream_t *s) {
    if (s->app_gone) return;
    s->app_gone = 1;
    s->send_dead = 1;
    if (e->conn && !s->quic_closed)
        (void)ngtcp2_conn_shutdown_stream(e->conn, 0, s->id, 0);
}

/* moves what the app wrote into the send ring */
static void stream_read_app(hy2_engine_t *e, hy2_stream_t *s) {
    while (!s->local_eof && !s->send_dead) {
        uint64_t used = s->send_put - s->send_acked;
        if (used >= HY2_RING_SIZE) return;
        size_t start = (size_t)(s->send_put & (HY2_RING_SIZE - 1));
        size_t room = HY2_RING_SIZE - (size_t)used;
        if (room > HY2_RING_SIZE - start) room = HY2_RING_SIZE - start;
        ssize_t n = read(s->fd, s->send_ring + start, room);
        if (n > 0) {
            s->send_put += (uint64_t)n;
            continue;
        }
        if (n == 0) {
            s->local_eof = 1;
            return;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        stream_abort_app(e, s);
        return;
    }
}

/* moves received stream bytes to the app and opens the windows again */
static void stream_write_app(hy2_engine_t *e, hy2_stream_t *s) {
    while (!s->app_gone && s->recv_out < s->recv_in) {
        size_t start = (size_t)(s->recv_out & (HY2_RING_SIZE - 1));
        size_t have = (size_t)(s->recv_in - s->recv_out);
        if (have > HY2_RING_SIZE - start) have = HY2_RING_SIZE - start;
        ssize_t n = write(s->fd, s->recv_ring + start, have);
        if (n > 0) {
            s->recv_out += (uint64_t)n;
            if (e->conn && !s->quic_closed) {
                (void)ngtcp2_conn_extend_max_stream_offset(e->conn, s->id, (uint64_t)n);
                ngtcp2_conn_extend_max_offset(e->conn, (uint64_t)n);
            }
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        stream_abort_app(e, s);
        return;
    }
    if (!s->app_gone && s->remote_fin && s->recv_out == s->recv_in && !s->wr_shut) {
        (void)shutdown(s->fd, SHUT_WR);
        s->wr_shut = 1;
    }
}

static int stream_wants_send(const hy2_stream_t *s) {
    if (s->send_dead || s->blocked) return 0;
    if (s->send_put > s->send_sent) return 1;
    return s->local_eof && !s->fin_sent;
}

static void attach_stream(hy2_engine_t *e, int fd, hy2_endpoint_t *endpoint) {
    hy2_stream_t *s = NULL;
    for (size_t i = 0; i < HY2_STREAMS_MAX; ++i)
        if (!e->streams[i].in_use) {
            s = &e->streams[i];
            break;
        }
    int64_t id = -1;
    if (s && ngtcp2_conn_open_bidi_stream(e->conn, &id, NULL) != 0) s = NULL;
    uint8_t *send_ring = s ? malloc(HY2_RING_SIZE) : NULL;
    uint8_t *recv_ring = s ? malloc(HY2_RING_SIZE) : NULL;
    if (!s || !send_ring || !recv_ring) {
        free(send_ring);
        free(recv_ring);
        endpoint->aborted = 1;
        __sync_synchronize();
        close(fd);
        endpoint_release(endpoint);
        return;
    }
    memset(s, 0, sizeof *s);
    s->in_use = 1;
    s->fd = fd;
    s->id = id;
    s->endpoint = endpoint;
    s->send_ring = send_ring;
    s->recv_ring = recv_ring;
}

static void streams_drop(hy2_engine_t *e, int abort_all) {
    for (size_t i = 0; i < HY2_STREAMS_MAX; ++i) {
        hy2_stream_t *s = &e->streams[i];
        if (!s->in_use) continue;
        int clean = !abort_all && s->quic_closed && !s->reset_by_peer;
        stream_free(s, !clean);
    }
}

/* ---- tls ----------------------------------------------------------------- */

static int verify_callback(int ok, X509_STORE_CTX *ctx) {
    if (ok) return ok;
    SSL *ssl = X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
    ngtcp2_crypto_conn_ref *ref = ssl ? SSL_get_app_data(ssl) : NULL;
    hy2_engine_t *e = ref ? ref->user_data : NULL;
    if (e)
        snprintf(e->tls_detail, sizeof e->tls_detail, "certificate rejected: %s",
                 X509_verify_cert_error_string(X509_STORE_CTX_get_error(ctx)));
    return ok;
}

static SSL_CTX *tls_context_new(const hy2_engine_t *e, char *error, size_t cap) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        snprintf(error, cap, "tls context could not be created");
        return NULL;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
    /* the hybrid post quantum share is 1.2 kb and spills the hello into a
       second initial packet, which servers answer with a retry */
    SSL_CTX_set1_groups_list(ctx, "X25519:P-256");
    if (e->has_pin) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, verify_callback);
        if (SSL_CTX_load_verify_locations(ctx, e->config.ca_file, NULL) != 1) {
            snprintf(error, cap, "tls roots unavailable at %s", e->config.ca_file);
            SSL_CTX_free(ctx);
            return NULL;
        }
    }
    return ctx;
}

static int pin_matches(hy2_engine_t *e) {
    X509 *cert = SSL_get1_peer_certificate(e->ssl);
    if (!cert) return 0;
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    int ok = X509_digest(cert, EVP_sha256(), digest, &digest_len) == 1 &&
             digest_len == sizeof e->pin && memcmp(digest, e->pin, sizeof e->pin) == 0;
    X509_free(cert);
    return ok;
}

/* ---- http/3 authentication ----------------------------------------------- */

static void h3_rand(uint8_t *dest, size_t destlen) {
    if (RAND_bytes(dest, (int)destlen) != 1) memset(dest, 0, destlen);
}

static int h3_recv_data(nghttp3_conn *conn, int64_t stream_id, const uint8_t *data,
                        size_t datalen, void *user, void *stream_user) {
    (void)conn; (void)data; (void)stream_user;
    hy2_engine_t *e = user;
    (void)ngtcp2_conn_extend_max_stream_offset(e->conn, stream_id, datalen);
    ngtcp2_conn_extend_max_offset(e->conn, datalen);
    return 0;
}

static int h3_deferred_consume(nghttp3_conn *conn, int64_t stream_id, size_t consumed,
                               void *user, void *stream_user) {
    return h3_recv_data(conn, stream_id, NULL, consumed, user, stream_user);
}

static int h3_recv_header(nghttp3_conn *conn, int64_t stream_id, int32_t token,
                          nghttp3_rcbuf *name, nghttp3_rcbuf *value, uint8_t flags,
                          void *user, void *stream_user) {
    (void)conn; (void)name; (void)flags; (void)stream_user;
    hy2_engine_t *e = user;
    if (stream_id != e->auth_stream || token != NGHTTP3_QPACK_TOKEN__STATUS) return 0;
    nghttp3_vec v = nghttp3_rcbuf_get_buf(value);
    if (v.len == 3 && memcmp(v.base, "233", 3) == 0) {
        e->auth_done = 1;
        return 0;
    }
    detail_set(e, "authentication rejected (http status %.*s)", (int)(v.len > 8 ? 8 : v.len),
               (const char *)v.base);
    return NGHTTP3_ERR_CALLBACK_FAILURE;
}

static int h3_stop_sending(nghttp3_conn *conn, int64_t stream_id, uint64_t code,
                           void *user, void *stream_user) {
    (void)conn; (void)stream_user;
    hy2_engine_t *e = user;
    return ngtcp2_conn_shutdown_stream_read(e->conn, 0, stream_id, code) == 0
        ? 0 : NGHTTP3_ERR_CALLBACK_FAILURE;
}

static int h3_reset_stream(nghttp3_conn *conn, int64_t stream_id, uint64_t code,
                           void *user, void *stream_user) {
    (void)conn; (void)stream_user;
    hy2_engine_t *e = user;
    return ngtcp2_conn_shutdown_stream_write(e->conn, 0, stream_id, code) == 0
        ? 0 : NGHTTP3_ERR_CALLBACK_FAILURE;
}

/* the request the server's auth handler expects: POST https://hysteria/auth */
static int h3_start(hy2_engine_t *e) {
    if (ngtcp2_conn_get_streams_uni_left(e->conn) < 3) {
        detail_set(e, "the server allows too few unidirectional streams for http/3");
        return -1;
    }
    static const nghttp3_callbacks callbacks = {
        .recv_data = h3_recv_data,
        .deferred_consume = h3_deferred_consume,
        .recv_header = h3_recv_header,
        .stop_sending = h3_stop_sending,
        .reset_stream = h3_reset_stream,
        .rand = h3_rand,
    };
    nghttp3_settings settings;
    nghttp3_settings_default(&settings);
    if (nghttp3_conn_client_new(&e->h3, &callbacks, &settings, nghttp3_mem_default(), e) != 0) {
        detail_set(e, "http/3 could not start");
        return -1;
    }
    int64_t control, qpack_encoder, qpack_decoder;
    if (ngtcp2_conn_open_uni_stream(e->conn, &control, NULL) != 0 ||
        nghttp3_conn_bind_control_stream(e->h3, control) != 0 ||
        ngtcp2_conn_open_uni_stream(e->conn, &qpack_encoder, NULL) != 0 ||
        ngtcp2_conn_open_uni_stream(e->conn, &qpack_decoder, NULL) != 0 ||
        nghttp3_conn_bind_qpack_streams(e->h3, qpack_encoder, qpack_decoder) != 0) {
        detail_set(e, "http/3 control streams could not open");
        return -1;
    }
    if (ngtcp2_conn_open_bidi_stream(e->conn, &e->auth_stream, NULL) != 0) {
        detail_set(e, "the authentication stream could not open");
        return -1;
    }
    static const uint8_t rx_rate[] = "0";
    nghttp3_nv headers[] = {
        { (const uint8_t *)":method", (const uint8_t *)"POST", 7, 4, NGHTTP3_NV_FLAG_NONE },
        { (const uint8_t *)":scheme", (const uint8_t *)"https", 7, 5, NGHTTP3_NV_FLAG_NONE },
        { (const uint8_t *)":authority", (const uint8_t *)"hysteria", 10, 8, NGHTTP3_NV_FLAG_NONE },
        { (const uint8_t *)":path", (const uint8_t *)"/auth", 5, 5, NGHTTP3_NV_FLAG_NONE },
        { (const uint8_t *)HY2_AUTH_HEADER, (const uint8_t *)e->auth, sizeof HY2_AUTH_HEADER - 1,
          strlen(e->auth), NGHTTP3_NV_FLAG_NO_COPY_NAME | NGHTTP3_NV_FLAG_NEVER_INDEX },
        /* zero asks the server for bbr, since the client states no rate */
        { (const uint8_t *)"hysteria-cc-rx", rx_rate, 14, 1, NGHTTP3_NV_FLAG_NONE },
    };
    if (nghttp3_conn_submit_request(e->h3, e->auth_stream, headers,
                                    sizeof headers / sizeof headers[0], NULL, NULL) != 0) {
        detail_set(e, "the authentication request could not be queued");
        return -1;
    }
    return 0;
}

/* ---- quic callbacks ------------------------------------------------------ */

static ngtcp2_conn *get_conn(ngtcp2_crypto_conn_ref *ref) {
    hy2_engine_t *e = ref->user_data;
    return e->conn;
}

static void cb_rand(uint8_t *dest, size_t destlen, const ngtcp2_rand_ctx *ctx) {
    (void)ctx;
    h3_rand(dest, destlen);
}

static int cb_new_connection_id(ngtcp2_conn *conn, ngtcp2_cid *cid,
                                ngtcp2_stateless_reset_token *token, size_t cidlen,
                                void *user) {
    (void)conn; (void)user;
    if (RAND_bytes(cid->data, (int)cidlen) != 1 ||
        RAND_bytes(token->data, (int)sizeof token->data) != 1)
        return NGTCP2_ERR_CALLBACK_FAILURE;
    cid->datalen = cidlen;
    return 0;
}

static int cb_handshake_completed(ngtcp2_conn *conn, void *user) {
    (void)conn;
    hy2_engine_t *e = user;
    e->handshake_done = 1;
    if (e->has_pin && !pin_matches(e)) {
        detail_set(e, "certificate rejected: it does not match the pinned sha-256");
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    return h3_start(e) == 0 ? 0 : NGTCP2_ERR_CALLBACK_FAILURE;
}

static int stream_is_h3(const hy2_engine_t *e, int64_t id) {
    return (id & 2) || id == e->auth_stream;
}

static int cb_recv_stream_data(ngtcp2_conn *conn, uint32_t flags, int64_t id,
                               uint64_t offset, const uint8_t *data, size_t len,
                               void *user, void *stream_user) {
    (void)conn; (void)offset; (void)stream_user;
    hy2_engine_t *e = user;
    int fin = (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0;
    if (stream_is_h3(e, id)) {
        if (!e->h3) return 0;
        nghttp3_ssize used = nghttp3_conn_read_stream2(e->h3, id, data, len, fin,
                                                       ngtcp2_conn_get_timestamp(e->conn));
        if (used < 0) {
            if (e->detail[0] == '\0')
                detail_set(e, "http/3 failed: %s", nghttp3_strerror((int)used));
            return NGTCP2_ERR_CALLBACK_FAILURE;
        }
        (void)ngtcp2_conn_extend_max_stream_offset(e->conn, id, (uint64_t)used);
        ngtcp2_conn_extend_max_offset(e->conn, (uint64_t)used);
        return 0;
    }
    hy2_stream_t *s = stream_by_id(e, id);
    if (!s) {
        (void)ngtcp2_conn_extend_max_stream_offset(e->conn, id, len);
        ngtcp2_conn_extend_max_offset(e->conn, len);
        return 0;
    }
    if (s->recv_in - s->recv_out + len > HY2_RING_SIZE) {
        detail_set(e, "the server sent more than the stream window allows");
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    ring_copy_in(s->recv_ring, s->recv_in, data, len);
    s->recv_in += len;
    if (fin) s->remote_fin = 1;
    return 0;
}

static int cb_acked_stream_data_offset(ngtcp2_conn *conn, int64_t id, uint64_t offset,
                                       uint64_t len, void *user, void *stream_user) {
    (void)conn; (void)stream_user;
    hy2_engine_t *e = user;
    if (stream_is_h3(e, id))
        return e->h3 && nghttp3_conn_add_ack_offset(e->h3, id, len) != 0
            ? NGTCP2_ERR_CALLBACK_FAILURE : 0;
    hy2_stream_t *s = stream_by_id(e, id);
    if (s && offset + len > s->send_acked) s->send_acked = offset + len;
    return 0;
}

static int cb_extend_max_stream_data(ngtcp2_conn *conn, int64_t id, uint64_t max_data,
                                     void *user, void *stream_user) {
    (void)conn; (void)max_data; (void)stream_user;
    hy2_engine_t *e = user;
    if (stream_is_h3(e, id))
        return e->h3 && nghttp3_conn_unblock_stream(e->h3, id) != 0
            ? NGTCP2_ERR_CALLBACK_FAILURE : 0;
    hy2_stream_t *s = stream_by_id(e, id);
    if (s) s->blocked = 0;
    return 0;
}

static int cb_stream_close(ngtcp2_conn *conn, uint32_t flags, int64_t id,
                           uint64_t rx_code, uint64_t tx_code, void *user,
                           void *stream_user) {
    (void)conn; (void)stream_user;
    hy2_engine_t *e = user;
    if (stream_is_h3(e, id)) {
        if (!e->h3) return 0;
        uint32_t h3_flags = NGHTTP3_STREAM_CLOSE_FLAG_NONE;
        if (flags & NGTCP2_STREAM_CLOSE2_FLAG_RX_APP_ERROR_CODE_SET)
            h3_flags |= NGHTTP3_STREAM_CLOSE_FLAG_RX_APP_ERROR_CODE_SET;
        if (flags & NGTCP2_STREAM_CLOSE2_FLAG_TX_APP_ERROR_CODE_SET)
            h3_flags |= NGHTTP3_STREAM_CLOSE_FLAG_TX_APP_ERROR_CODE_SET;
        int rv = nghttp3_conn_close_stream2(e->h3, h3_flags, id, rx_code, tx_code);
        if (rv != 0 && rv != NGHTTP3_ERR_STREAM_NOT_FOUND) return NGTCP2_ERR_CALLBACK_FAILURE;
        if (id == e->auth_stream && !e->auth_done && e->detail[0] == '\0')
            detail_set(e, "the server closed the authentication stream without a status");
        return 0;
    }
    hy2_stream_t *s = stream_by_id(e, id);
    if (!s) return 0;
    s->quic_closed = 1;
    if (flags & NGTCP2_STREAM_CLOSE2_FLAG_RX_APP_ERROR_CODE_SET) s->reset_by_peer = 1;
    return 0;
}

static int cb_stream_reset(ngtcp2_conn *conn, int64_t id, uint64_t final_size,
                           uint64_t code, void *user, void *stream_user) {
    (void)conn; (void)final_size; (void)code; (void)stream_user;
    hy2_engine_t *e = user;
    if (stream_is_h3(e, id))
        return e->h3 && nghttp3_conn_shutdown_stream_read(e->h3, id) != 0
            ? NGTCP2_ERR_CALLBACK_FAILURE : 0;
    hy2_stream_t *s = stream_by_id(e, id);
    if (s) s->reset_by_peer = 1;
    return 0;
}

static int cb_stream_stop_sending(ngtcp2_conn *conn, int64_t id, uint64_t code,
                                  void *user, void *stream_user) {
    (void)conn; (void)code; (void)stream_user;
    hy2_engine_t *e = user;
    if (stream_is_h3(e, id))
        return e->h3 && nghttp3_conn_shutdown_stream_read(e->h3, id) != 0
            ? NGTCP2_ERR_CALLBACK_FAILURE : 0;
    hy2_stream_t *s = stream_by_id(e, id);
    if (s) s->send_dead = 1;
    return 0;
}

/* ---- the quic connection ------------------------------------------------- */

static void conn_drop(hy2_engine_t *e, int graceful, int abort_streams) {
    if (e->conn && graceful) {
        uint8_t packet[HY2_UDP_PACKET_MAX];
        uint8_t wire[HY2_UDP_PACKET_MAX + HY2_SALAMANDER_SALT_LEN];
        ngtcp2_path_storage storage;
        ngtcp2_pkt_info info;
        ngtcp2_ccerr ccerr;
        ngtcp2_path_storage_zero(&storage);
        ngtcp2_ccerr_default(&ccerr);
        ngtcp2_ssize n = ngtcp2_conn_write_connection_close(e->conn, &storage.path, &info,
                                                            packet, sizeof packet, &ccerr, now_ts());
        if (n > 0 && e->udp_fd >= 0) {
            const uint8_t *out = packet;
            size_t out_len = (size_t)n;
            uint8_t salt[HY2_SALAMANDER_SALT_LEN];
            size_t wire_len = 0;
            if (e->obfs[0] && RAND_bytes(salt, sizeof salt) == 1 &&
                hy2_salamander_wrap((const uint8_t *)e->obfs, strlen(e->obfs), salt, packet,
                                    out_len, wire, sizeof wire, &wire_len) == HY2_OK) {
                out = wire;
                out_len = wire_len;
            }
            /* best effort, the server drops idle connections on its own */
            (void)send(e->udp_fd, out, out_len, 0);
        }
    }
    streams_drop(e, abort_streams);
    if (e->h3) nghttp3_conn_del(e->h3);
    e->h3 = NULL;
    if (e->conn) ngtcp2_conn_del(e->conn);
    e->conn = NULL;
    if (e->ssl) {
        SSL_set_app_data(e->ssl, NULL);
        SSL_free(e->ssl);
    }
    e->ssl = NULL;
    if (e->octx) ngtcp2_crypto_ossl_ctx_del(e->octx);
    e->octx = NULL;
    if (e->udp_fd >= 0) close(e->udp_fd);
    e->udp_fd = -1;
    e->handshake_done = 0;
    e->auth_done = 0;
    e->auth_stream = -1;
    e->conn_dead = 0;
}

static int udp_open(hy2_engine_t *e, size_t index) {
    const uint8_t *address = e->addresses[index];
    int family = e->address_lens[index] == 4 ? AF_INET : AF_INET6;
    memset(&e->remote, 0, sizeof e->remote);
    if (family == AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)&e->remote;
        in->sin_family = AF_INET;
        in->sin_port = htons(e->config.port);
        memcpy(&in->sin_addr, address, 4);
        e->remote_len = sizeof *in;
    } else {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&e->remote;
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(e->config.port);
        memcpy(&in6->sin6_addr, address, 16);
        e->remote_len = sizeof *in6;
    }
    int fd = socket(family, SOCK_DGRAM, 0);
    if (fd < 0) {
        detail_set(e, "udp socket failed: %s", strerror(errno));
        return -1;
    }
    int size = 256 * 1024;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof size);
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof size);
    e->local_len = sizeof e->local;
    if (set_nonblocking(fd) != 0 ||
        connect(fd, (struct sockaddr *)&e->remote, e->remote_len) != 0 ||
        getsockname(fd, (struct sockaddr *)&e->local, &e->local_len) != 0) {
        detail_set(e, "udp socket setup failed: %s", strerror(errno));
        close(fd);
        return -1;
    }
    e->udp_fd = fd;
    return 0;
}

static int conn_open(hy2_engine_t *e, size_t index) {
    e->detail[0] = '\0';
    e->tls_detail[0] = '\0';
    e->auth_stream = -1;
    if (udp_open(e, index) != 0) return -1;

    e->ssl = SSL_new(e->ssl_ctx);
    if (!e->ssl || ngtcp2_crypto_ossl_ctx_new(&e->octx, e->ssl) != 0 ||
        ngtcp2_crypto_ossl_configure_client_session(e->ssl) != 0) {
        detail_set(e, "tls session could not be created");
        conn_drop(e, 0, 1);
        return -1;
    }
    e->conn_ref.get_conn = get_conn;
    e->conn_ref.user_data = e;
    SSL_set_app_data(e->ssl, &e->conn_ref);
    SSL_set_connect_state(e->ssl);
    static const unsigned char alpn[] = "\x02h3";
    SSL_set_alpn_protos(e->ssl, alpn, sizeof alpn - 1);
    const char *name = e->sni[0] ? e->sni : e->peer_host;
    if (is_ip_literal(name)) {
        if (!e->has_pin)
            X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(e->ssl), name);
    } else {
        SSL_set_tlsext_host_name(e->ssl, name);
        if (!e->has_pin) SSL_set1_host(e->ssl, name);
    }

    static const ngtcp2_callbacks callbacks = {
        .client_initial = ngtcp2_crypto_client_initial_cb,
        .recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb,
        .handshake_completed = cb_handshake_completed,
        .encrypt = ngtcp2_crypto_encrypt_cb,
        .decrypt = ngtcp2_crypto_decrypt_cb,
        .hp_mask = ngtcp2_crypto_hp_mask_cb,
        .recv_stream_data = cb_recv_stream_data,
        .acked_stream_data_offset = cb_acked_stream_data_offset,
        .stream_close2 = cb_stream_close,
        .recv_retry = ngtcp2_crypto_recv_retry_cb,
        .rand = cb_rand,
        .get_new_connection_id2 = cb_new_connection_id,
        .update_key = ngtcp2_crypto_update_key_cb,
        .stream_reset = cb_stream_reset,
        .extend_max_stream_data = cb_extend_max_stream_data,
        .delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
        .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
        .stream_stop_sending = cb_stream_stop_sending,
        .version_negotiation = ngtcp2_crypto_version_negotiation_cb,
        .get_path_challenge_data2 = ngtcp2_crypto_get_path_challenge_data2_cb,
    };
    ngtcp2_cid scid, dcid;
    scid.datalen = 17;
    dcid.datalen = 18;
    if (RAND_bytes(scid.data, (int)scid.datalen) != 1 ||
        RAND_bytes(dcid.data, (int)dcid.datalen) != 1) {
        detail_set(e, "random bytes unavailable");
        conn_drop(e, 0, 1);
        return -1;
    }
    ngtcp2_settings settings;
    ngtcp2_settings_default(&settings);
    settings.initial_ts = now_ts();
    settings.cc_algo = NGTCP2_CC_ALGO_BBR;
    /* path mtu discovery needs the dont fragment bit, which ios 5 sockets
       cannot set, so the 1200 byte minimum stays */
    settings.no_pmtud = 1;
    settings.max_tx_udp_payload_size = 1200;
    settings.handshake_timeout = (ngtcp2_duration)e->attempt_ms * NGTCP2_MILLISECONDS;
    ngtcp2_transport_params params;
    ngtcp2_transport_params_default(&params);
    params.initial_max_stream_data_bidi_local = HY2_RING_SIZE;
    params.initial_max_stream_data_uni = 64 * 1024;
    params.initial_max_data = HY2_CONN_WINDOW;
    params.initial_max_streams_bidi = 0;
    params.initial_max_streams_uni = 16;
    params.max_idle_timeout = (ngtcp2_duration)HY2_IDLE_TIMEOUT_MS * NGTCP2_MILLISECONDS;
    params.active_connection_id_limit = 4;
    e->path.local.addr = (ngtcp2_sockaddr *)&e->local;
    e->path.local.addrlen = e->local_len;
    e->path.remote.addr = (ngtcp2_sockaddr *)&e->remote;
    e->path.remote.addrlen = e->remote_len;
    e->path.user_data = NULL;
    if (ngtcp2_conn_client_new(&e->conn, &dcid, &scid, &e->path, NGTCP2_PROTO_VER_V1,
                               &callbacks, &settings, &params, NULL, e) != 0) {
        detail_set(e, "quic connection could not be created");
        conn_drop(e, 0, 1);
        return -1;
    }
    ngtcp2_conn_set_tls_native_handle(e->conn, e->octx);
    ngtcp2_conn_set_keep_alive_timeout(e->conn, (ngtcp2_duration)HY2_KEEPALIVE_MS * NGTCP2_MILLISECONDS);
    e->attempt_deadline_ms = senko_now_ms() + e->attempt_ms;
    e->last_rx_ms = senko_now_ms();
    e->refused = 0;
    return 0;
}

static void packet_send(hy2_engine_t *e, const uint8_t *data, size_t len) {
    const uint8_t *out = data;
    size_t out_len = len;
    uint8_t wire[HY2_UDP_PACKET_MAX + HY2_SALAMANDER_SALT_LEN];
    if (e->obfs[0]) {
        uint8_t salt[HY2_SALAMANDER_SALT_LEN];
        if (RAND_bytes(salt, sizeof salt) != 1 ||
            hy2_salamander_wrap((const uint8_t *)e->obfs, strlen(e->obfs), salt, data, len,
                                wire, sizeof wire, &out_len) != HY2_OK)
            return;
        out = wire;
    }
    /* udp may drop a packet at any hop, so quic already retransmits it; a
       refused send is the same loss. a dead path ends in the idle timeout */
    (void)send(e->udp_fd, out, out_len, 0);
}

/* nghttp3 owns the request and control streams; the rest go to the app flows */
static hy2_stream_t *pick_stream(hy2_engine_t *e) {
    for (size_t step = 1; step <= HY2_STREAMS_MAX; ++step) {
        size_t i = (e->round_robin + step) % HY2_STREAMS_MAX;
        hy2_stream_t *s = &e->streams[i];
        if (s->in_use && s->skip_gen != e->flush_gen && stream_wants_send(s)) {
            e->round_robin = i;
            return s;
        }
    }
    return NULL;
}

/* writes packets until quic has nothing ready or the burst is spent */
static int flush_packets(hy2_engine_t *e) {
    uint8_t packet[HY2_UDP_PACKET_MAX];
    ngtcp2_path_storage storage;
    ngtcp2_pkt_info info;
    ngtcp2_tstamp ts = now_ts();
    size_t written = 0;
    ++e->flush_gen;
    e->more_to_send = 0;
    for (size_t burst = 0; burst < HY2_WRITE_BURST; ++burst) {
        int64_t stream_id = -1;
        int fin = 0;
        nghttp3_vec h3_vec[8];
        ngtcp2_vec vec[8];
        size_t vec_count = 0;
        hy2_stream_t *own = NULL;
        size_t own_avail = 0;

        if (e->h3 && ngtcp2_conn_get_max_data_left2(e->conn)) {
            nghttp3_ssize count = nghttp3_conn_writev_stream(e->h3, &stream_id, &fin, h3_vec, 8);
            if (count < 0) {
                detail_set(e, "http/3 write failed: %s", nghttp3_strerror((int)count));
                return -1;
            }
            for (nghttp3_ssize i = 0; i < count; ++i) {
                vec[i].base = h3_vec[i].base;
                vec[i].len = h3_vec[i].len;
            }
            vec_count = (size_t)count;
        }
        if (stream_id < 0 && e->auth_done && ngtcp2_conn_get_max_data_left2(e->conn)) {
            own = pick_stream(e);
            if (own) {
                stream_id = own->id;
                own_avail = (size_t)(own->send_put - own->send_sent);
                size_t start = (size_t)(own->send_sent & (HY2_RING_SIZE - 1));
                size_t first = HY2_RING_SIZE - start;
                if (first > own_avail) first = own_avail;
                if (first) {
                    vec[vec_count].base = own->send_ring + start;
                    vec[vec_count].len = first;
                    ++vec_count;
                }
                if (own_avail > first) {
                    vec[vec_count].base = own->send_ring;
                    vec[vec_count].len = own_avail - first;
                    ++vec_count;
                }
                fin = own->local_eof;
            }
        }

        uint32_t flags = fin ? NGTCP2_WRITE_STREAM_FLAG_FIN : NGTCP2_WRITE_STREAM_FLAG_NONE;
        ngtcp2_ssize data_len = -1;
        ngtcp2_path_storage_zero(&storage);
        ngtcp2_ssize n = ngtcp2_conn_writev_stream(e->conn, &storage.path, &info, packet,
                                                   sizeof packet, &data_len, flags, stream_id,
                                                   vec_count ? vec : NULL, vec_count, ts);
        if (n < 0) {
            if (n == NGTCP2_ERR_STREAM_DATA_BLOCKED) {
                if (own) own->blocked = 1;
                else if (e->h3) nghttp3_conn_block_stream(e->h3, stream_id);
                continue;
            }
            if (n == NGTCP2_ERR_STREAM_SHUT_WR || n == NGTCP2_ERR_STREAM_NOT_FOUND) {
                if (own) own->send_dead = 1;
                else if (e->h3) nghttp3_conn_shutdown_stream_write(e->h3, stream_id);
                continue;
            }
            if (e->detail[0] == '\0')
                detail_set(e, "quic write failed: %s", ngtcp2_strerror((int)n));
            return -1;
        }
        if (data_len >= 0 && stream_id >= 0) {
            if (own) {
                own->send_sent += (uint64_t)data_len;
                if (fin && (size_t)data_len >= own_avail) own->fin_sent = 1;
            } else if (e->h3 && nghttp3_conn_add_write_offset(e->h3, stream_id, (size_t)data_len) != 0) {
                detail_set(e, "http/3 write offset failed");
                return -1;
            }
        } else if (own) {
            own->skip_gen = e->flush_gen;
        }
        if (n == 0) break;
        packet_send(e, packet, (size_t)n);
        ++written;
        if (burst + 1 == HY2_WRITE_BURST) e->more_to_send = 1;
    }
    if (written) ngtcp2_conn_update_pkt_tx_time(e->conn, ts);
    return 0;
}

static int conn_read_packets(hy2_engine_t *e) {
    uint8_t raw[HY2_UDP_PACKET_MAX];
    uint8_t plain[HY2_UDP_PACKET_MAX];
    for (int i = 0; i < HY2_READ_BURST; ++i) {
        ssize_t n = recv(e->udp_fd, raw, sizeof raw, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            /* an icmp port unreachable surfaces here on a connected socket. it can
               be forged, so after the handshake it only ends the connection when
               the server has also been silent for a while */
            if (errno == ECONNREFUSED) {
                if (!e->handshake_done) {
                    detail_set(e, "nothing listens on this udp port");
                    return -1;
                }
                e->refused = 1;
            }
            return 0;
        }
        const uint8_t *data = raw;
        size_t len = (size_t)n;
        if (e->obfs[0]) {
            if (hy2_salamander_unwrap((const uint8_t *)e->obfs, strlen(e->obfs), raw, len,
                                      plain, sizeof plain, &len) != HY2_OK)
                continue;
            data = plain;
        }
        ngtcp2_pkt_info info;
        memset(&info, 0, sizeof info);
        int rv = ngtcp2_conn_read_pkt(e->conn, &e->path, &info, data, len, now_ts());
        e->refused = 0;
        e->last_rx_ms = senko_now_ms();
        if (rv != 0) {
            if (e->detail[0] == '\0') {
                if (rv == NGTCP2_ERR_DRAINING) {
                    const ngtcp2_ccerr *ccerr = ngtcp2_conn_get_ccerr(e->conn);
                    if (ccerr->type == NGTCP2_CCERR_TYPE_TRANSPORT &&
                        ccerr->error_code >= 0x100 && ccerr->error_code <= 0x1ff)
                        detail_set(e, "the server refused the tls handshake (alert %u)",
                                   (unsigned)(ccerr->error_code - 0x100));
                    else
                        detail_set(e, "the server closed the connection (code %llu%s%.60s)",
                                   (unsigned long long)ccerr->error_code,
                                   ccerr->reasonlen ? ": " : "",
                                   ccerr->reasonlen ? (const char *)ccerr->reason : "");
                } else if (rv == NGTCP2_ERR_CRYPTO) {
                    detail_set(e, "%s", e->tls_detail[0] ? e->tls_detail : "tls handshake failed");
                } else {
                    detail_set(e, "quic read failed: %s", ngtcp2_strerror(rv));
                }
            }
            return -1;
        }
    }
    return 0;
}

/* ---- connection life ----------------------------------------------------- */

static void state_set(hy2_engine_t *e, hy2_state_t state, const char *error) {
    pthread_mutex_lock(&e->lock);
    e->state = state;
    if (error) snprintf(e->last_error, sizeof e->last_error, "%s", error);
    if (state == HY2_STATE_FAILED) e->retry_at_ms = senko_now_ms() + HY2_RETRY_COOLDOWN_MS;
    if (state != HY2_STATE_CONNECTING) {
        e->want_connect = 0;
        e->attempting = 0;
    }
    pthread_cond_broadcast(&e->changed);
    pthread_mutex_unlock(&e->lock);
}

static void address_text(const hy2_engine_t *e, size_t index, char *out, size_t cap) {
    int family = e->address_lens[index] == 4 ? AF_INET : AF_INET6;
    if (!inet_ntop(family, e->addresses[index], out, (socklen_t)cap)) snprintf(out, cap, "?");
}

/* a connection ended before it carried anything, so try the next address or
   give up with the reason of the last failure */
static void attempt_failed(hy2_engine_t *e, size_t index) {
    char where[INET6_ADDRSTRLEN];
    char message[HY2_ERROR_MAX];
    address_text(e, index, where, sizeof where);
    const char *why = e->tls_detail[0] && strncmp(e->detail, "tls", 3) == 0
        ? e->tls_detail : e->detail;
    snprintf(message, sizeof message, "hysteria2 server %s port %u: %s", where,
             (unsigned)e->config.port, why[0] ? why : "no answer, udp may be blocked");
    fprintf(stderr, "senkod: %s\n", message);
    conn_drop(e, 0, 1);
    if (e->tries_left > 0) return;
    state_set(e, HY2_STATE_FAILED, message);
}

static void begin_attempt(hy2_engine_t *e) {
    while (e->tries_left > 0) {
        size_t index = e->cursor % e->config.address_count;
        e->cursor = (e->cursor + 1) % e->config.address_count;
        --e->tries_left;
        if (conn_open(e, index) == 0) {
            e->detail[0] = '\0';
            return;
        }
        attempt_failed(e, index);
        if (e->tries_left == 0) return;
    }
}

static size_t current_index(const hy2_engine_t *e) {
    return (e->cursor + e->config.address_count - 1) % e->config.address_count;
}

static void take_pending(hy2_engine_t *e) {
    hy2_pending_t batch[HY2_PENDING_MAX];
    pthread_mutex_lock(&e->lock);
    size_t count = e->pending_count;
    memcpy(batch, e->pending, count * sizeof batch[0]);
    e->pending_count = 0;
    pthread_mutex_unlock(&e->lock);
    for (size_t i = 0; i < count; ++i) {
        if (e->conn && e->auth_done && !e->conn_dead) {
            attach_stream(e, batch[i].fd, batch[i].endpoint);
        } else {
            batch[i].endpoint->aborted = 1;
            __sync_synchronize();
            close(batch[i].fd);
            endpoint_release(batch[i].endpoint);
        }
    }
}

static void service_streams(hy2_engine_t *e, const struct pollfd *fds,
                            hy2_stream_t *const *owners, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        hy2_stream_t *s = owners[i];
        if (!s->in_use) continue;
        if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) stream_read_app(e, s);
        if (fds[i].revents & (POLLOUT | POLLERR)) stream_write_app(e, s);
    }
    for (size_t i = 0; i < HY2_STREAMS_MAX; ++i) {
        hy2_stream_t *s = &e->streams[i];
        if (!s->in_use) continue;
        stream_write_app(e, s);
        stream_reap(s);
    }
}

static void *engine_main(void *arg) {
    hy2_engine_t *e = arg;
    struct pollfd fds[2 + HY2_STREAMS_MAX];
    hy2_stream_t *owners[HY2_STREAMS_MAX];

    for (;;) {
        pthread_mutex_lock(&e->lock);
        int stop = e->stop;
        int want = e->want_connect;
        pthread_mutex_unlock(&e->lock);
        if (stop) break;

        if (!e->conn && want) {
            if (!e->attempting) {
                e->tries_left = e->config.address_count < 2 ? e->config.address_count : 2;
                e->attempting = 1;
            }
            begin_attempt(e);
        }

        int64_t now = senko_now_ms();
        int64_t wait_ms = HY2_POLL_MAX_MS;
        nfds_t nfds = 0;
        fds[nfds].fd = e->wake[0];
        fds[nfds].events = POLLIN;
        ++nfds;
        size_t owned = 0;
        if (e->conn) {
            fds[nfds].fd = e->udp_fd;
            fds[nfds].events = POLLIN;
            ++nfds;
            ngtcp2_tstamp expiry = ngtcp2_conn_get_expiry2(e->conn);
            ngtcp2_tstamp ts = now_ts();
            wait_ms = expiry <= ts ? 0 : (int64_t)((expiry - ts + 999999) / 1000000);
            if (!e->auth_done && e->attempt_deadline_ms - now < wait_ms)
                wait_ms = e->attempt_deadline_ms - now;
            if (e->more_to_send) wait_ms = 0;
            for (size_t i = 0; i < HY2_STREAMS_MAX; ++i) {
                hy2_stream_t *s = &e->streams[i];
                if (!s->in_use || s->app_gone) continue;
                short events = 0;
                if (!s->local_eof && !s->send_dead && s->send_put - s->send_acked < HY2_RING_SIZE)
                    events |= POLLIN;
                if (s->recv_out < s->recv_in) events |= POLLOUT;
                if (!events) continue;
                fds[nfds].fd = s->fd;
                fds[nfds].events = events;
                owners[owned++] = s;
                ++nfds;
            }
        }
        if (wait_ms < 0) wait_ms = 0;
        if (wait_ms > HY2_POLL_MAX_MS) wait_ms = HY2_POLL_MAX_MS;
        for (nfds_t i = 0; i < nfds; ++i) fds[i].revents = 0;
        int polled = poll(fds, nfds, (int)wait_ms);
        if (polled < 0 && errno != EINTR) break;

        if (polled > 0 && (fds[0].revents & POLLIN)) {
            uint8_t sink[64];
            while (read(e->wake[0], sink, sizeof sink) > 0) {}
        }
        take_pending(e);
        if (!e->conn) continue;

        int failed = 0;
        if (polled > 0 && (fds[1].revents & (POLLIN | POLLERR)) && conn_read_packets(e) != 0) failed = 1;
        if (!failed && ngtcp2_conn_get_expiry2(e->conn) <= now_ts()) {
            int rv = ngtcp2_conn_handle_expiry(e->conn, now_ts());
            if (rv != 0) {
                if (e->detail[0] == '\0') {
                    if (rv == NGTCP2_ERR_HANDSHAKE_TIMEOUT)
                        detail_set(e, "no answer, udp may be blocked%s",
                                   e->obfs[0] ? " or the obfuscation password differs" : "");
                    else if (rv == NGTCP2_ERR_IDLE_CLOSE)
                        detail_set(e, "the connection went idle");
                    else
                        detail_set(e, "%s", ngtcp2_strerror(rv));
                }
                failed = 1;
            }
        }
        if (!failed && polled > 0 && owned) {
            service_streams(e, fds + 2, owners, owned);
        } else if (!failed) {
            service_streams(e, NULL, NULL, 0);
        }
        if (!failed && flush_packets(e) != 0) failed = 1;
        if (!failed) {
            if (ngtcp2_conn_in_closing_period(e->conn) || ngtcp2_conn_in_draining_period(e->conn)) {
                if (e->detail[0] == '\0') detail_set(e, "the server closed the connection");
                failed = 1;
            }
        }

        if (!failed && e->refused && senko_now_ms() - e->last_rx_ms >= HY2_REFUSED_GRACE_MS) {
            detail_set(e, "the server stopped answering (icmp port unreachable)");
            failed = 1;
        }
        if (!failed && e->auth_done) {
            pthread_mutex_lock(&e->lock);
            int announce = e->state != HY2_STATE_READY;
            pthread_mutex_unlock(&e->lock);
            if (announce) {
                char where[INET6_ADDRSTRLEN];
                address_text(e, current_index(e), where, sizeof where);
                fprintf(stderr, "senkod: hysteria2 connected to %s port %u\n", where,
                        (unsigned)e->config.port);
                state_set(e, HY2_STATE_READY, NULL);
            }
        }
        if (!failed && !e->auth_done && senko_now_ms() >= e->attempt_deadline_ms) {
            if (e->detail[0] == '\0') detail_set(e, "no answer, udp may be blocked");
            failed = 1;
        }

        if (failed) {
            pthread_mutex_lock(&e->lock);
            int ready = e->state == HY2_STATE_READY;
            pthread_mutex_unlock(&e->lock);
            if (ready) {
                char message[HY2_ERROR_MAX];
                snprintf(message, sizeof message, "hysteria2 connection lost: %s", e->detail);
                fprintf(stderr, "senkod: %s\n", message);
                conn_drop(e, 0, 1);
                state_set(e, HY2_STATE_IDLE, message);
            } else {
                attempt_failed(e, current_index(e));
            }
        }
    }

    conn_drop(e, 1, 1);
    take_pending(e);
    return NULL;
}

/* ---- public API ---------------------------------------------------------- */

static int copy_text(char *dst, size_t cap, const char *src) {
    if (!src) src = "";
    size_t len = strlen(src);
    if (len >= cap) return -1;
    memcpy(dst, src, len + 1);
    return 0;
}

static pthread_once_t crypto_once = PTHREAD_ONCE_INIT;
static int crypto_ready;

static void crypto_init(void) {
    crypto_ready = ngtcp2_crypto_ossl_init() == 0;
}

hy2_engine_t *hy2_engine_start(const hy2_engine_config_t *config, char *error, size_t error_cap) {
    if (error && error_cap) error[0] = '\0';
    if (!config || !error || error_cap == 0 || !config->addresses || !config->address_lens ||
        config->address_count == 0 || config->address_count > 16 || config->port == 0 ||
        !config->auth || !config->ca_file || config->connect_timeout_ms <= 0) {
        if (error && error_cap) snprintf(error, error_cap, "the hysteria2 configuration is incomplete");
        return NULL;
    }
    hy2_engine_t *e = calloc(1, sizeof *e);
    if (!e) {
        snprintf(error, error_cap, "not enough memory for the hysteria2 engine");
        return NULL;
    }
    e->config = *config;
    e->udp_fd = -1;
    e->auth_stream = -1;
    e->wake[0] = e->wake[1] = -1;
    for (size_t i = 0; i < config->address_count; ++i) {
        if (config->address_lens[i] != 4 && config->address_lens[i] != 16) {
            snprintf(error, error_cap, "a server address has an unusable length");
            free(e);
            return NULL;
        }
        memcpy(e->addresses[i], config->addresses[i], 16);
        e->address_lens[i] = config->address_lens[i];
    }
    e->config.addresses = (const uint8_t (*)[16])e->addresses;
    e->config.address_lens = e->address_lens;
    if (copy_text(e->sni, sizeof e->sni, config->sni) != 0 ||
        copy_text(e->peer_host, sizeof e->peer_host, config->peer_host) != 0 ||
        copy_text(e->auth, sizeof e->auth, config->auth) != 0 ||
        copy_text(e->obfs, sizeof e->obfs, config->obfs_password) != 0 ||
        (!e->sni[0] && !e->peer_host[0])) {
        snprintf(error, error_cap, "a hysteria2 field is empty or too long");
        free(e);
        return NULL;
    }
    if (config->pin_sha256 && config->pin_sha256[0]) {
        if (pin_parse(config->pin_sha256, e->pin) != 0) {
            snprintf(error, error_cap, "pinSHA256 is not a sha-256 digest in hex");
            free(e);
            return NULL;
        }
        e->has_pin = 1;
    }
    e->attempt_ms = config->connect_timeout_ms / (config->address_count > 1 ? 2 : 1);

    pthread_once(&crypto_once, crypto_init);
    if (!crypto_ready) {
        snprintf(error, error_cap, "the quic crypto layer could not start");
        free(e);
        return NULL;
    }
    e->ssl_ctx = tls_context_new(e, error, error_cap);
    if (!e->ssl_ctx) {
        free(e);
        return NULL;
    }
    if (pipe(e->wake) != 0 || set_nonblocking(e->wake[0]) != 0 || set_nonblocking(e->wake[1]) != 0 ||
        pthread_mutex_init(&e->lock, NULL) != 0) {
        snprintf(error, error_cap, "the hysteria2 engine could not allocate its wakeup pipe");
        if (e->wake[0] >= 0) close(e->wake[0]);
        if (e->wake[1] >= 0) close(e->wake[1]);
        SSL_CTX_free(e->ssl_ctx);
        free(e);
        return NULL;
    }
    if (pthread_cond_init(&e->changed, NULL) != 0) {
        snprintf(error, error_cap, "the hysteria2 engine could not allocate its condition variable");
        pthread_mutex_destroy(&e->lock);
        close(e->wake[0]);
        close(e->wake[1]);
        SSL_CTX_free(e->ssl_ctx);
        free(e);
        return NULL;
    }
    e->state = HY2_STATE_CONNECTING;
    e->want_connect = 1;
    if (pthread_create(&e->thread, NULL, engine_main, e) != 0) {
        snprintf(error, error_cap, "the hysteria2 engine thread could not start");
        pthread_cond_destroy(&e->changed);
        pthread_mutex_destroy(&e->lock);
        close(e->wake[0]);
        close(e->wake[1]);
        SSL_CTX_free(e->ssl_ctx);
        free(e);
        return NULL;
    }
    e->thread_started = 1;
    return e;
}

int hy2_engine_dial(void *context, char *error, size_t error_cap) {
    hy2_engine_t *e = context;
    if (error && error_cap) error[0] = '\0';
    if (!e || !error || error_cap == 0) return -1;
    /* the engine ends its own attempts at connect_timeout_ms; the extra second
       lets that failure, which names a cause, arrive before this wait gives up */
    int64_t deadline = senko_now_ms() + e->config.connect_timeout_ms + 1000;

    pthread_mutex_lock(&e->lock);
    for (;;) {
        if (e->stop) {
            snprintf(error, error_cap, "the server connect was cancelled");
            goto refused;
        }
        if (e->state == HY2_STATE_READY) break;
        if (e->state == HY2_STATE_IDLE ||
            (e->state == HY2_STATE_FAILED && senko_now_ms() >= e->retry_at_ms)) {
            e->state = HY2_STATE_CONNECTING;
            e->want_connect = 1;
            wake_thread(e);
        } else if (e->state == HY2_STATE_FAILED) {
            snprintf(error, error_cap, "%s", e->last_error);
            goto refused;
        }
        int64_t left = deadline - senko_now_ms();
        if (left <= 0) {
            snprintf(error, error_cap, "hysteria2 did not answer within %d ms",
                     e->config.connect_timeout_ms);
            goto refused;
        }
        /* short slices keep the wait independent of the wall clock */
        struct timespec until;
        struct timeval now;
        gettimeofday(&now, NULL);
        long slice = left > 100 ? 100 : (long)left;
        until.tv_sec = now.tv_sec + slice / 1000;
        until.tv_nsec = ((long)now.tv_usec + (slice % 1000) * 1000) * 1000L;
        if (until.tv_nsec >= 1000000000L) {
            until.tv_sec += 1;
            until.tv_nsec -= 1000000000L;
        }
        (void)pthread_cond_timedwait(&e->changed, &e->lock, &until);
    }
    if (e->pending_count >= HY2_PENDING_MAX) {
        snprintf(error, error_cap, "too many hysteria2 streams are opening at once");
        goto refused;
    }

    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        snprintf(error, error_cap, "cannot create a stream channel: %s", strerror(errno));
        goto refused;
    }
    hy2_endpoint_t *endpoint = calloc(1, sizeof *endpoint);
    if (!endpoint || pair[0] >= FD_SETSIZE || set_nonblocking(pair[0]) != 0 ||
        set_nonblocking(pair[1]) != 0) {
        snprintf(error, error_cap, "cannot prepare a stream channel");
        free(endpoint);
        close(pair[0]);
        close(pair[1]);
        goto refused;
    }
#ifdef SO_NOSIGPIPE
    int on = 1;
    (void)setsockopt(pair[0], SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
    (void)setsockopt(pair[1], SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
    endpoint->refs = 2;
    e->pending[e->pending_count].fd = pair[1];
    e->pending[e->pending_count].endpoint = endpoint;
    ++e->pending_count;
    pthread_mutex_unlock(&e->lock);

    pthread_mutex_lock(&registry_lock);
    /* a descriptor the opener closed before it called open leaves a stale entry */
    endpoint_release(registry[pair[0]]);
    registry[pair[0]] = endpoint;
    pthread_mutex_unlock(&registry_lock);
    wake_thread(e);
    return pair[0];

refused:
    pthread_mutex_unlock(&e->lock);
    return -1;
}

void hy2_engine_cancel(hy2_engine_t *e) {
    if (!e) return;
    pthread_mutex_lock(&e->lock);
    e->stop = 1;
    pthread_cond_broadcast(&e->changed);
    pthread_mutex_unlock(&e->lock);
    wake_thread(e);
}

void hy2_engine_stop(hy2_engine_t *e) {
    if (!e) return;
    hy2_engine_cancel(e);
    if (e->thread_started) pthread_join(e->thread, NULL);
    SSL_CTX_free(e->ssl_ctx);
    pthread_cond_destroy(&e->changed);
    pthread_mutex_destroy(&e->lock);
    close(e->wake[0]);
    close(e->wake[1]);
    free(e);
}
