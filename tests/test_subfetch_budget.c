#define _POSIX_C_SOURCE 200809L

/* the dialer used to keep a budget of its own, so a stalling tunnel spent the
   whole fetch timeout before the request even started and the app gave up on
   REFRESH/ADDSUB first. subfetch now hands the dialer what is left instead */
#include "subfetch.h"
#include "transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int failed;

static void ok(const char *what, int pass) {
    if (pass) return;
    fprintf(stderr, "FAIL %s\n", what);
    failed++;
}

static long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static int g_calls;
static int g_budget;
static int g_stall_ms;
static char g_host[256];

static int stub_dial(void *ctx, const char *host, uint16_t port, int budget_ms) {
    (void)ctx; (void)port;
    g_calls++;
    g_budget = budget_ms;
    snprintf(g_host, sizeof g_host, "%s", host ? host : "");
    if (g_stall_ms > 0) poll(NULL, 0, g_stall_ms); /* stand in for a stalled tunnel dial */
    return -1; /* only the budget handed down matters here */
}

/* hands subfetch one end of a socketpair whose other end already holds the
   canned response */
static const char *g_response;
static int g_peer = -1;
static int g_keep_open;

static int canned_dial(void *ctx, const char *host, uint16_t port, int budget_ms) {
    (void)ctx; (void)host; (void)port; (void)budget_ms;
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return -1;
    if (write(sv[1], g_response, strlen(g_response)) != (ssize_t)strlen(g_response)) {
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    if (!g_keep_open) shutdown(sv[1], SHUT_WR);
    if (fcntl(sv[0], F_SETFL, O_NONBLOCK) != 0) {
        close(sv[0]);
        close(sv[1]);
        return -1;
    }
    g_peer = sv[1];
    return sv[0];
}

int main(void) {
    static uint8_t body[4096];
    size_t blen = 0;

    subfetch_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.dial = stub_dial;
    cfg.tcp = &transport_tcp;
    cfg.tls = &transport_tls;
    cfg.max_redirects = 5;

    g_calls = 0; g_budget = -1; g_stall_ms = 0;
    subfetch_status_t r = subfetch_get(&cfg, "http://1.2.3.4/sub",
                                       body, sizeof body, &blen, 2000);
    ok("a failed dial is reported as a dial error", r == SUBFETCH_ERR_DIAL);
    ok("the dialer is called once", g_calls == 1);
    ok("the dialer is told what is left of the fetch",
       g_budget > 0 && g_budget <= 2000);

/* a dialer that spends its whole share must not push the fetch past the
   timeout the caller is waiting on */
    g_calls = 0; g_budget = -1; g_stall_ms = 900;
    long started = now_ms();
    r = subfetch_get(&cfg, "http://1.2.3.4/sub", body, sizeof body, &blen, 1000);
    long spent = now_ms() - started;
    ok("a stalling dial still fails", r == SUBFETCH_ERR_DIAL);
    ok("the fetch stays inside its own timeout", spent < 2000);

/* resolving the host before dial() defeats a tunnel that would resolve it
   remotely: a blocked local resolver used to fail every fetch that a working
   tunnel could otherwise reach */
    g_calls = 0; g_host[0] = '\0';
    r = subfetch_get(&cfg, "http://sub.example.com/feed", body, sizeof body, &blen, 2000);
    ok("a failed dial is reported as a dial error", r == SUBFETCH_ERR_DIAL);
    ok("dial gets the hostname from the url, not a resolved ip",
       strcmp(g_host, "sub.example.com") == 0);

/* a dead panel answers 404 with a json body; the status is what the user
   needs to see instead of a bare "fetch failed" */
    subfetch_info_t info;
    cfg.dial = canned_dial;
    g_response = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n"
                 "Content-Length: 18\r\n\r\n{\"isFound\":false}\r\n";
    r = subfetch_get_info(&cfg, "http://sub.example.com/feed", body, sizeof body,
                          &blen, 2000, &info);
    close(g_peer);
    ok("a 404 is an http error", r == SUBFETCH_ERR_HTTP);
    ok("the 404 status is reported", info.http_status == 404);

    g_response = "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nabcd";
    r = subfetch_get_info(&cfg, "http://sub.example.com/feed", body, sizeof body,
                          &blen, 2000, &info);
    close(g_peer);
    ok("a 200 is fetched", r == SUBFETCH_OK && blen == 4 && memcmp(body, "abcd", 4) == 0);
    ok("a success carries no error status", info.http_status == 0);

    g_keep_open = 1;
    g_response = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabcd";
    r = subfetch_get_info(&cfg, "http://sub.example.com/feed", body, sizeof body,
                          &blen, 50, &info);
    close(g_peer);
    ok("a stalled body reports its HTTP status and progress",
       r == SUBFETCH_ERR_TRANSPORT && info.timed_out &&
       info.http_status == 200 && info.received == 4 && blen == 0);
    g_response = "";
    r = subfetch_get_info(&cfg, "http://sub.example.com/feed", body, sizeof body,
                          &blen, 50, &info);
    close(g_peer);
    ok("a silent server is distinguished from a stalled body",
       r == SUBFETCH_ERR_TRANSPORT && info.timed_out &&
       info.http_status == 0 && info.received == 0 && blen == 0);
    g_keep_open = 0;

    g_response = "garbage\r\n\r\n";
    r = subfetch_get_info(&cfg, "http://sub.example.com/feed", body, sizeof body,
                          &blen, 2000, &info);
    close(g_peer);
    ok("a response that is not http has no status", r == SUBFETCH_ERR_HTTP && info.http_status == 0);

/* aggregator feeds run past the buffer; the caller gets the first part */
    uint8_t small[6];
    g_response = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabcdefghij";
    r = subfetch_get_info(&cfg, "http://sub.example.com/feed", small, sizeof small,
                          &blen, 2000, &info);
    close(g_peer);
    ok("a long body is cut, not failed",
       r == SUBFETCH_OK && info.body_cut && blen == 6 && memcmp(small, "abcdef", 6) == 0);
    g_response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nabcd\r\n4\r\nefgh\r\n0\r\n\r\n";
    r = subfetch_get_info(&cfg, "http://sub.example.com/feed", small, sizeof small,
                          &blen, 2000, &info);
    close(g_peer);
    ok("a long chunked body is cut", r == SUBFETCH_OK && info.body_cut && blen == 6);
    g_response = "HTTP/1.1 200 OK\r\nContent-Length: 104694742\r\n\r\nabcdefghij";
    r = subfetch_get_info(&cfg, "http://sub.example.com/feed", small, sizeof small,
                          &blen, 2000, &info);
    close(g_peer);
    ok("a 100 MB feed is cut, not unreadable", r == SUBFETCH_OK && info.body_cut && blen == 6);
    g_response = "HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999\r\n\r\nabcdefghij";
    r = subfetch_get_info(&cfg, "http://sub.example.com/feed", small, sizeof small,
                          &blen, 2000, &info);
    close(g_peer);
    ok("a length past any long is refused", r == SUBFETCH_ERR_HTTP);
    g_response = "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nabcd";
    r = subfetch_get_info(&cfg, "http://sub.example.com/feed", small, sizeof small,
                          &blen, 2000, &info);
    close(g_peer);
    ok("a body that fits is not cut", r == SUBFETCH_OK && !info.body_cut && blen == 4);
    g_response = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabcdefghij";
    r = subfetch_get(&cfg, "http://sub.example.com/feed", small, sizeof small, &blen, 2000);
    close(g_peer);
    ok("without info a cut body still fails", r == SUBFETCH_ERR_TOOBIG);

/* a geo dat goes to a file as it arrives: the bytes written are the bytes sent,
   a body past the limit is refused, and an error page writes nothing */
    {
        char path[] = "/tmp/senko-subfile-XXXXXX";
        int fd = mkstemp(path);
        size_t written = 0;
        char back[64];
        FILE *out;
        ok("temp file", fd >= 0);
        out = fdopen(fd, "w+b");
        g_response = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789";
        r = subfetch_get_file(&cfg, "http://sub.example.com/geo.dat", out, 100, &written, 2000, &info);
        close(g_peer);
        fflush(out);
        rewind(out);
        memset(back, 0, sizeof back);
        ok("a download is written to the file", r == SUBFETCH_OK && written == 10 &&
           fread(back, 1, sizeof back, out) == 10 && memcmp(back, "0123456789", 10) == 0);

        g_response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nabcd\r\n3\r\nefg\r\n0\r\n\r\n";
        rewind(out);
        r = subfetch_get_file(&cfg, "http://sub.example.com/geo.dat", out, 100, &written, 2000, &info);
        close(g_peer);
        ok("a chunked download is written", r == SUBFETCH_OK && written == 7);

        g_response = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789";
        r = subfetch_get_file(&cfg, "http://sub.example.com/geo.dat", out, 6, &written, 2000, &info);
        close(g_peer);
        ok("a download past the limit is refused", r == SUBFETCH_ERR_TOOBIG);

        g_response = "HTTP/1.1 404 Not Found\r\nContent-Length: 5\r\n\r\nnope!";
        r = subfetch_get_file(&cfg, "http://sub.example.com/geo.dat", out, 100, &written, 2000, &info);
        close(g_peer);
        ok("an error page is an http error", r == SUBFETCH_ERR_HTTP && info.http_status == 404 && written == 0);

        ok("no file", subfetch_get_file(&cfg, "http://sub.example.com/geo.dat", NULL, 100, &written, 2000, &info) ==
           SUBFETCH_ERR_ARG);
        ok("no limit", subfetch_get_file(&cfg, "http://sub.example.com/geo.dat", out, 0, &written, 2000, &info) ==
           SUBFETCH_ERR_ARG);
        fclose(out);
        unlink(path);
    }

    /* the routing header of a happ panel comes back with the body */
    g_response = "HTTP/1.1 200 OK\r\nContent-Length: 4\r\nRouting: happ://routing/onadd/e30=\r\n\r\nabcd";
    r = subfetch_get_info(&cfg, "http://sub.example.com/feed", body, sizeof body, &blen, 2000, &info);
    close(g_peer);
    ok("the routing header is returned", r == SUBFETCH_OK &&
       strcmp(info.routing, "happ://routing/onadd/e30=") == 0 && !info.routing_too_long);

    if (failed) {
        printf("%d subfetch budget checks failed\n", failed);
        return 1;
    }
    printf("all subfetch budget checks passed\n");
    return 0;
}
