/* integration probe: drives daemon/hy2_engine.c against a real hysteria2
   server and a local tcp echo target. not part of `make test`, which must not
   depend on another program; tests/hy2_probe.sh starts the server and runs it.

   env: HY2_PORT HY2_AUTH HY2_CA and optionally HY2_SNI HY2_PIN HY2_OBFS.
   argv[1]: transfer (default), abort or reject */
#define _DEFAULT_SOURCE
#include "hy2_engine.h"
#include "hy2_proto.h"
#include "senko_time.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define STREAMS 8
#define BYTES_PER_STREAM (3 * 1024 * 1024)

static int failed;

#define CHECK(cond, label) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s (line %d)\n", label, __LINE__); failed = 1; } \
} while (0)

static int echo_listener;
static uint16_t echo_port;

static void *echo_conn(void *arg) {
    int fd = (int)(intptr_t)arg;
    uint8_t buf[16384];
    for (;;) {
        ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) break;
        size_t off = 0;
        while (off < (size_t)n) {
            ssize_t w = write(fd, buf + off, (size_t)n - off);
            if (w <= 0) goto done;
            off += (size_t)w;
        }
    }
done:
    close(fd);
    return NULL;
}

static void *echo_main(void *arg) {
    (void)arg;
    for (;;) {
        int fd = accept(echo_listener, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) continue;
            return NULL;
        }
        pthread_t t;
        pthread_create(&t, NULL, echo_conn, (void *)(intptr_t)fd);
        pthread_detach(t);
    }
}

static void echo_start(void) {
    echo_listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int on = 1;
    setsockopt(echo_listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    if (bind(echo_listener, (struct sockaddr *)&a, sizeof a) != 0 || listen(echo_listener, 16) != 0) {
        perror("echo bind");
        exit(2);
    }
    socklen_t len = sizeof a;
    getsockname(echo_listener, (struct sockaddr *)&a, &len);
    echo_port = ntohs(a.sin_port);
    pthread_t t;
    pthread_create(&t, NULL, echo_main, NULL);
    pthread_detach(t);
}

typedef struct {
    void *th;
    int fd;
    uint8_t *out;
    uint8_t *in;
    size_t sent;
    size_t got;
    size_t total;
    int head_done;
    hy2_tcp_response_t head;
    uint8_t request[256];
    size_t request_len;
    size_t request_sent;
    int result; /* 0 running, 1 done, -1 failed */
    char why[2200];
} flow_t;

static int flow_open(flow_t *f, hy2_engine_t *engine, size_t total) {
    char err[256];
    memset(f, 0, sizeof *f);
    f->fd = hy2_engine_dial(engine, err, sizeof err);
    if (f->fd < 0) {
        fprintf(stderr, "dial failed: %s\n", err);
        return -1;
    }
    f->th = transport_hy2.open(f->fd, NULL);
    if (!f->th) return -1;
    f->total = total;
    f->out = malloc(total ? total : 1);
    f->in = malloc(total ? total : 1);
    for (size_t i = 0; i < total; ++i) f->out[i] = (uint8_t)((i * 2654435761u) >> 13);
    vless_dest_t dest;
    memset(&dest, 0, sizeof dest);
    dest.atyp = VLESS_ADDR_IPV4;
    dest.host_addr[0] = 127; dest.host_addr[3] = 1;
    dest.port = echo_port;
    CHECK(hy2_tcp_request_build(&dest, f->request, sizeof f->request, &f->request_len) == HY2_OK,
          "build request");
    hy2_tcp_response_init(&f->head);
    return 0;
}

/* returns 1 while there is more to do */
static int flow_step(flow_t *f) {
    if (f->result) return 0;
    if (f->request_sent < f->request_len) {
        int n = transport_hy2.write(f->th, f->request + f->request_sent, f->request_len - f->request_sent);
        if (n > 0) f->request_sent += (size_t)n;
        else if (n != TRANSPORT_WANT_WRITE) { f->result = -1; snprintf(f->why, sizeof f->why, "request write %d", n); return 0; }
    } else if (f->sent < f->total) {
        size_t chunk = f->total - f->sent;
        if (chunk > 32768) chunk = 32768;
        int n = transport_hy2.write(f->th, f->out + f->sent, chunk);
        if (n > 0) f->sent += (size_t)n;
        else if (n != TRANSPORT_WANT_WRITE) { f->result = -1; snprintf(f->why, sizeof f->why, "data write %d", n); return 0; }
    }
    uint8_t buf[32768];
    for (;;) {
        int n = transport_hy2.read(f->th, buf, sizeof buf);
        if (n == TRANSPORT_WANT_READ) break;
        if (n == TRANSPORT_EOF) {
            f->result = (f->got == f->total && f->head_done) ? 1 : -1;
            if (f->result < 0) snprintf(f->why, sizeof f->why, "eof after %zu of %zu", f->got, f->total);
            return 0;
        }
        if (n < 0) { f->result = -1; snprintf(f->why, sizeof f->why, "read error %d after %zu", n, f->got); return 0; }
        size_t off = 0;
        if (!f->head_done) {
            size_t used = 0;
            hy2_status_t st = hy2_tcp_response_feed(&f->head, buf, (size_t)n, &used);
            if (st == HY2_NEED_MORE) continue;
            if (st != HY2_OK) { f->result = -1; snprintf(f->why, sizeof f->why, "server said: %s", f->head.message); return 0; }
            f->head_done = 1;
            off = used;
        }
        size_t take = (size_t)n - off;
        if (f->got + take > f->total) { f->result = -1; snprintf(f->why, sizeof f->why, "more bytes than sent"); return 0; }
        memcpy(f->in + f->got, buf + off, take);
        f->got += take;
    }
    if (f->sent == f->total && f->request_sent == f->request_len && f->total && f->got == f->total) {
        f->result = memcmp(f->in, f->out, f->total) == 0 ? 1 : -1;
        if (f->result < 0) snprintf(f->why, sizeof f->why, "echo differs");
        return 0;
    }
    return 1;
}

static void flow_close(flow_t *f) {
    transport_hy2.close(f->th);
    close(f->fd);
    free(f->out);
    free(f->in);
}

static int wait_all(flow_t *flows, size_t count, int64_t limit_ms) {
    int64_t deadline = senko_now_ms() + limit_ms;
    for (;;) {
        int busy = 0;
        struct pollfd fds[STREAMS];
        for (size_t i = 0; i < count; ++i) {
            busy |= flow_step(&flows[i]);
            fds[i].fd = flows[i].fd;
            fds[i].events = POLLIN | (flows[i].sent < flows[i].total ? POLLOUT : 0);
        }
        if (!busy) return 0;
        if (senko_now_ms() > deadline) return -1;
        poll(fds, count, 50);
    }
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "transfer";
    const char *port = getenv("HY2_PORT");
    const char *auth = getenv("HY2_AUTH");
    const char *ca = getenv("HY2_CA");
    if (!port || !auth || !ca) {
        fprintf(stderr, "set HY2_PORT HY2_AUTH HY2_CA\n");
        return 2;
    }
    echo_start();

    uint8_t address[1][16] = {{127, 0, 0, 1}};
    uint8_t length[1] = {4};
    hy2_engine_config_t config;
    memset(&config, 0, sizeof config);
    config.addresses = address;
    config.address_lens = length;
    config.address_count = 1;
    config.port = (uint16_t)atoi(port);
    config.sni = getenv("HY2_SNI") ? getenv("HY2_SNI") : "localhost";
    config.peer_host = "localhost";
    config.auth = auth;
    config.pin_sha256 = getenv("HY2_PIN");
    config.obfs_password = getenv("HY2_OBFS");
    config.ca_file = ca;
    config.connect_timeout_ms = 6000;
    char err[256];
    hy2_engine_t *engine = hy2_engine_start(&config, err, sizeof err);
    if (!engine) {
        fprintf(stderr, "engine start failed: %s\n", err);
        return 2;
    }

    if (strcmp(mode, "reject") == 0) {
        int fd = hy2_engine_dial(engine, err, sizeof err);
        CHECK(fd < 0, "dial must fail");
        fprintf(stderr, "dial error: %s\n", err);
        CHECK(strstr(err, "authentication") != NULL || strstr(err, "certificate") != NULL ||
              strstr(err, "no answer") != NULL, "error names the cause");
        hy2_engine_stop(engine);
        puts(failed ? "FAIL" : "PASS reject");
        return failed;
    }

    if (strcmp(mode, "abort") == 0) {
        flow_t f;
        if (flow_open(&f, engine, 64 * 1024 * 1024) != 0) return 1;
        puts("STREAMING");
        fflush(stdout);
        int rc = wait_all(&f, 1, 45000);
        CHECK(rc == 0, "flow must end");
        CHECK(f.result == -1 && strncmp(f.why, "eof", 3) != 0, "an aborted stream is an error, not eof");
        fprintf(stderr, "abort result: %s\n", f.why);
        flow_close(&f);
        /* the next dial reconnects, and fails while the server stays down */
        int fd = hy2_engine_dial(engine, err, sizeof err);
        CHECK(fd < 0, "dial after the server died");
        fprintf(stderr, "redial error: %s\n", err);
        hy2_engine_stop(engine);
        puts(failed ? "FAIL" : "PASS abort");
        return failed;
    }

    flow_t flows[STREAMS];
    int64_t t0 = senko_now_ms();
    for (size_t i = 0; i < STREAMS; ++i)
        if (flow_open(&flows[i], engine, BYTES_PER_STREAM) != 0) return 1;
    int rc = wait_all(flows, STREAMS, 60000);
    CHECK(rc == 0, "all flows finish");
    for (size_t i = 0; i < STREAMS; ++i) {
        CHECK(flows[i].result == 1, "flow result");
        if (flows[i].result != 1) fprintf(stderr, "flow %zu: %s\n", i, flows[i].why);
    }
    int64_t ms = senko_now_ms() - t0;
    printf("moved %d bytes each way over %d streams in %lld ms\n", BYTES_PER_STREAM * STREAMS, STREAMS,
           (long long)ms);
    for (size_t i = 0; i < STREAMS; ++i) flow_close(&flows[i]);

    /* a clean close: the app finishes, the echo target closes, the stream ends in eof */
    flow_t one;
    if (flow_open(&one, engine, 1000) != 0) return 1;
    CHECK(wait_all(&one, 1, 10000) == 0 && one.result == 1, "small flow");
    shutdown(one.fd, SHUT_WR);
    uint8_t tail[16];
    int64_t until = senko_now_ms() + 5000;
    int n = TRANSPORT_WANT_READ;
    while (n == TRANSPORT_WANT_READ && senko_now_ms() < until) {
        n = transport_hy2.read(one.th, tail, sizeof tail);
        usleep(10000);
    }
    CHECK(n == TRANSPORT_EOF, "clean end reads as eof");
    flow_close(&one);

    hy2_engine_stop(engine);
    puts(failed ? "FAIL" : "PASS transfer");
    return failed;
}
