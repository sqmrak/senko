#define _DEFAULT_SOURCE

#include "real_probe.h"
#include "senko_time.h"
#include "senko_clock.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int g_fail = 0;
static void ok(const char *what, int cond) {
    if (cond) return;
    g_fail++;
    fprintf(stderr, "FAIL %s\n", what);
}

typedef enum {
    FAKE_ANSWER = 0,  /* a proxy that carries the request and gets 204 */
    FAKE_CLOSE,       /* a proxy that hangs up after the greeting */
    FAKE_GARBAGE,     /* a proxy whose target speaks something else */
    FAKE_HTTP_ERROR,
    FAKE_TRUNCATED,
    FAKE_SILENT       /* a proxy that accepts and never answers */
} fake_mode_t;

typedef struct {
    int listen_fd;
    fake_mode_t mode;
    int connect_ok;   /* the CONNECT bytes were exactly the socks5 domain form */
    int request_ok;   /* the http request asked the probe host for 204 */
} fake_proxy_t;

static int read_exact(int fd, uint8_t *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, buf + got, len - got);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

static void *fake_proxy_main(void *arg) {
    fake_proxy_t *p = (fake_proxy_t *)arg;
    int fd = accept(p->listen_fd, NULL, NULL);
    if (fd < 0) return NULL;
    uint8_t greet[3];
    if (read_exact(fd, greet, sizeof greet) != 0) goto out;
    if (p->mode == FAKE_CLOSE) goto out;
    static const uint8_t chose[2] = { 0x05, 0x00 };
    if (write(fd, chose, sizeof chose) != (ssize_t)sizeof chose) goto out;

    static const char host[] = REAL_PROBE_HOST;
    uint8_t want[4 + 1 + sizeof host - 1 + 2];
    size_t w = 0;
    want[w++] = 0x05; want[w++] = 0x01; want[w++] = 0x00; want[w++] = 0x03;
    want[w++] = (uint8_t)(sizeof host - 1);
    memcpy(want + w, host, sizeof host - 1);
    w += sizeof host - 1;
    want[w++] = 0x00; want[w++] = REAL_PROBE_PORT;
    uint8_t got[sizeof want];
    if (read_exact(fd, got, sizeof got) != 0) goto out;
    p->connect_ok = memcmp(got, want, sizeof want) == 0;
    static const uint8_t granted[10] = { 0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0 };
    if (write(fd, granted, sizeof granted) != (ssize_t)sizeof granted) goto out;

    char req[512];
    size_t rl = 0;
    while (rl + 1 < sizeof req) {
        ssize_t n = read(fd, req + rl, sizeof req - 1 - rl);
        if (n <= 0) goto out;
        rl += (size_t)n;
        req[rl] = '\0';
        if (strstr(req, "\r\n\r\n")) break;
    }
    p->request_ok = strncmp(req, "GET /generate_204 HTTP/1.1\r\n", 28) == 0 &&
                    strstr(req, "\r\nHost: " REAL_PROBE_HOST "\r\n") != NULL;
    if (p->mode == FAKE_SILENT) {
        char drain[64];
        while (read(fd, drain, sizeof drain) > 0) {}
        goto out;
    }
    const char *answer = p->mode == FAKE_GARBAGE ? "SSH-2.0-OpenSSH\r\n"
        : p->mode == FAKE_HTTP_ERROR ? "HTTP/1.1 503 Service Unavailable\r\nDate: Sun, 06 Nov 1994 08:49:37 GMT\r\n\r\n"
        : p->mode == FAKE_TRUNCATED ? "HTTP/1.1 204 No Content\r\n"
        : "HTTP/1.1 204 No Content\r\n\r\n";
    (void)!write(fd, answer, strlen(answer));
out:
    close(fd);
    return NULL;
}

static int listen_local(uint16_t *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t al = sizeof a;
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) != 0 ||
        listen(fd, 4) != 0 || getsockname(fd, (struct sockaddr *)&a, &al) != 0)
        return -1;
    *port = ntohs(a.sin_port);
    return fd;
}

static void socks_profile(vl_server_t *s, uint16_t port) {
    memset(s, 0, sizeof *s);
    s->proto = VL_PROTO_SOCKS5;
    s->net = VL_NET_TCP;
    s->security = VL_SEC_NONE;
    snprintf(s->host, sizeof s->host, "127.0.0.1");
    s->port = port;
}

static int g_binds = 0;
static void count_bind(int fd) {
    (void)fd;
    g_binds++;
}

static int run_mode(fake_mode_t mode, int timeout_ms, fake_proxy_t *out,
                    char *reason, size_t cap, int64_t *elapsed) {
    uint16_t port = 0;
    memset(out, 0, sizeof *out);
    out->mode = mode;
    out->listen_fd = listen_local(&port);
    if (out->listen_fd < 0) return -2;
    pthread_t t;
    pthread_create(&t, NULL, fake_proxy_main, out);
    vl_server_t s;
    socks_profile(&s, port);
    int64_t start = senko_now_ms();
    int ms = real_probe_run(&s, "127.0.0.1", count_bind, timeout_ms, NULL, reason, cap);
    *elapsed = senko_now_ms() - start;
    /* a silent proxy keeps reading until the probe hangs up */
    pthread_join(t, NULL);
    close(out->listen_fd);
    return ms;
}

int main(void) {
    char reason[320];
    fake_proxy_t p;
    int64_t elapsed = 0;

    int ms = run_mode(FAKE_ANSWER, 2000, &p, reason, sizeof reason, &elapsed);
    ok("answer measured", ms >= 0 && ms <= elapsed);
    ok("connect bytes use the socks5 domain type", p.connect_ok);
    ok("request asks the probe host", p.request_ok);
    ok("bind hook ran before connect", g_binds == 1);

    ms = run_mode(FAKE_CLOSE, 2000, &p, reason, sizeof reason, &elapsed);
    ok("closed proxy fails", ms == -1 && reason[0]);

    ms = run_mode(FAKE_GARBAGE, 2000, &p, reason, sizeof reason, &elapsed);
    ok("non http answer fails", ms == -1 &&
       strstr(reason, "not SOCKS5") != NULL);

    ms = run_mode(FAKE_SILENT, 400, &p, reason, sizeof reason, &elapsed);
    ok("silent proxy times out", ms == -1 &&
       strstr(reason, "did not return an answer") != NULL);
    ok("timeout is bounded", elapsed >= 350 && elapsed < 1500);

    /* nothing listens on a port whose listener was just closed */
    uint16_t port = 0;
    int lf = listen_local(&port);
    close(lf);
    vl_server_t s;
    socks_profile(&s, port);
    ms = real_probe_run(&s, "127.0.0.1", NULL, 1000, NULL, reason, sizeof reason);
    ok("refused connect fails", ms == -1 && strstr(reason, "refused the connection on port") != NULL);

    s.proto = VL_PROTO_HYSTERIA2;
    ms = real_probe_run(&s, "127.0.0.1", NULL, 1000, NULL, reason, sizeof reason);
    ok("hysteria2 is not probed here", ms == -1 &&
       strcmp(reason, "unsupported transport") == 0);

    senko_clock_reset();
    ms = run_mode(FAKE_HTTP_ERROR, 2000, &p, reason, sizeof reason, &elapsed);
    ok("http error is not success or proof of bad credentials", ms == -1 && strstr(reason, "HTTP 503"));
    int64_t offset;
    ok("failed target date cannot poison clock", !senko_clock_offset(&offset));
    ms = run_mode(FAKE_TRUNCATED, 2000, &p, reason, sizeof reason, &elapsed);
    ok("incomplete 204 headers are rejected", ms == -1);
    const char *status = "HTTP/1.1 204 No Content\r\n";
    ok("status line accepted", real_probe_status(status, strlen(status)) == 204);
    ok("short answer rejected", real_probe_status("HTTP", 4) == -1);
    ok("other protocol rejected", real_probe_status("SSH-2.0", 7) == -1);
    ok("four digit status rejected", real_probe_status("HTTP/1.1 2040 OK", 15) == -1);
    ok("signed status rejected", real_probe_status("HTTP/1.1 +204 OK", 15) == -1);

    if (g_fail) return 1;
    printf("all real probe checks passed\n");
    return 0;
}
