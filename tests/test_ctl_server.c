#define _DEFAULT_SOURCE

#include "ctl_server.h"
#include "core/b64.h"
#include "core/control.h"
#include "core/store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

static int g_fail = 0;
static int g_verify_fail = 0;
static void ok(const char *what, int cond) {
    if (cond) return;
    g_fail++;
    fprintf(stderr, "FAIL %s\n", what);
}

typedef struct {
    int               calls;
    ctl_action_kind_t last_kind;
    int               last_index;
    char              last_host[256];
    int               fail_next;    /* force this many start failures */
} apply_rec_t;

static int mock_apply(void *ctx, const ctl_action_t *a) {
    apply_rec_t *r = (apply_rec_t *)ctx;
    r->calls++;
    r->last_kind = a->kind;
    r->last_index = a->server_index;
    snprintf(r->last_host, sizeof r->last_host, "%s", a->server.host);
    if (r->fail_next > 0) { r->fail_next--; return -1; }
    return 0;
}

/* canned fetch hook keeps refresh tests off the network */
typedef struct {
    int  calls;
    char last_url[512];
    const char *blob;
    int  fail_next;
} fetch_rec_t;

static fetch_rec_t g_fetch;

static int mock_fetch(void *ctx, const char *url,
                      unsigned char *buf, size_t cap, size_t *len) {
    (void)ctx;
    g_fetch.calls++;
    snprintf(g_fetch.last_url, sizeof g_fetch.last_url, "%s", url);
    if (g_fetch.fail_next) { g_fetch.fail_next = 0; return -1; }
    if (!g_fetch.blob) return -1;
    size_t bl = strlen(g_fetch.blob);
    if (bl > cap) return -1;
    memcpy(buf, g_fetch.blob, bl);
    *len = bl;
    return 0;
}

/* canned probe hook keeps ping tests deterministic */
typedef struct {
    int  calls;
    char last_host[256];
    uint16_t last_port;
    int  ret_ms;        /* canned rtt */
} probe_rec_t;

static probe_rec_t g_probe;

static int mock_probe(void *ctx, const char *host, uint16_t port) {
    (void)ctx;
    g_probe.calls++;
    snprintf(g_probe.last_host, sizeof g_probe.last_host, "%s", host);
    g_probe.last_port = port;
    return g_probe.ret_ms;
}

typedef struct {
    int calls;
    int ret_ms;
} tunnel_probe_rec_t;

static tunnel_probe_rec_t g_tunnel_probe;

static int mock_tunnel_probe(void *ctx) {
    (void)ctx;
    g_tunnel_probe.calls++;
    return g_tunnel_probe.ret_ms;
}

static int mock_verify_ok(void *ctx, char *reason, size_t reason_cap) {
    (void)ctx;
    if (g_verify_fail > 0) {
        g_verify_fail--;
        if (reason && reason_cap)
            snprintf(reason, reason_cap, "%s", "mock verify failed");
        return -1;
    }
    if (reason && reason_cap) reason[0] = '\0';
    return 0;
}

typedef struct {
    int calls;
    size_t last_n;
} persist_rec_t;

static persist_rec_t g_persist;

static void mock_persist(void *ctx, const store_t *store) {
    (void)ctx;
    g_persist.calls++;
    g_persist.last_n = store ? store->n : 0;
}

static int connect_unix(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return -1; }
    return fd;
}

static void token_path_from_sock(const char *sock, char *out, size_t cap) {
    size_t n = strlen(sock);
    if (n >= 5 && strcmp(sock + n - 5, ".sock") == 0 && n - 5 + 6 < cap) {
        memcpy(out, sock, n - 5);
        memcpy(out + n - 5, ".token", 7);
        return;
    }
    snprintf(out, cap, "%s.token", sock);
}

static void set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* drain after a few loop ticks because the client fd is non-blocking */
static size_t exchange(ctl_server_t *s, int cli, char *out, size_t cap) {
    for (int i = 0; i < 20; ++i) ctl_server_step(s, 2);
    size_t tot = 0;
    for (;;) {
        ssize_t n = read(cli, out + tot, cap - 1 - tot);
        if (n > 0) {
            tot += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        break;
    }
    out[tot] = '\0';
    return tot;
}

/* CONNECT without token must fail; proves the auth gate is live */
static int auth_client(ctl_server_t *s, int cli, const char *sock) {
    char tpath[160];
    char token[48];
    char line[96];
    char buf[256];
    token_path_from_sock(sock, tpath, sizeof tpath);
    FILE *f = fopen(tpath, "r");
    if (!f) return 0;
    if (!fgets(token, sizeof token, f)) { fclose(f); return 0; }
    fclose(f);
    size_t tl = strlen(token);
    while (tl > 0 && (token[tl - 1] == '\n' || token[tl - 1] == '\r'))
        token[--tl] = '\0';
    snprintf(line, sizeof line, "AUTH %s\n", token);
    write(cli, line, strlen(line));
    exchange(s, cli, buf, sizeof buf);
    return strstr(buf, "OK ") != NULL;
}

int main(void) {
    const char *path = "/tmp/senko_ctltest.sock";
    const char *block_path = "/tmp/senko_ctltest.block";

    unlink(block_path);
    FILE *block = fopen(block_path, "w");
    ok("block file create", block != NULL);
    if (block) {
        fputs("keep", block);
        fclose(block);
    }
    ctl_server_t blocked;
    ok("regular file blocks init",
       ctl_server_init(&blocked, block_path, mock_apply, NULL) == CTLS_ERR_BIND);
    struct stat bst;
    ok("regular file preserved", stat(block_path, &bst) == 0 && S_ISREG(bst.st_mode));
    unlink(block_path);

    const char *replace_path = "/tmp/senko_ctltest.replace";
    unlink(replace_path);
    ctl_server_t replaced;
    ok("replace-path init",
       ctl_server_init(&replaced, replace_path, mock_apply, NULL) == CTLS_OK);
    unlink(replace_path);
    block = fopen(replace_path, "w");
    ok("replacement file create", block != NULL);
    if (block) {
        fputs("keep", block);
        fclose(block);
    }
    ctl_server_close(&replaced);
    ok("replacement file preserved",
       stat(replace_path, &bst) == 0 && S_ISREG(bst.st_mode));
    unlink(replace_path);

    apply_rec_t rec;
    memset(&rec, 0, sizeof rec);

    ctl_server_t s;
    ok("server init", ctl_server_init(&s, path, mock_apply, &rec) == CTLS_OK);
    ctl_server_set_verify(&s, mock_verify_ok);

    size_t idx;
    store_add_manual(&s.engine.store,
        "vless://aaaa1111-6324-4d53-ad4f-8cda48b30811@1.1.1.1:443?security=none&type=tcp#A", &idx);
    store_add_manual(&s.engine.store,
        "vless://bbbb2222-6324-4d53-ad4f-8cda48b30811@2.2.2.2:8443?security=none&type=tcp#B", &idx);

    int cli = connect_unix(path);
    ok("client connect", cli >= 0);
    set_nonblock(cli);
    for (int i = 0; i < 10; ++i) ctl_server_step(&s, 2);
    ok("one client", ctl_server_client_count(&s) == 1);

    char buf[512];

    write(cli, "STATUS\n", 7);
    exchange(&s, cli, buf, sizeof buf);
    ok("status idle unauthed", strcmp(buf, "STATE idle\n") == 0);

    write(cli, "CONNECT 1\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("connect needs auth", strstr(buf, "auth required") != NULL ||
                            strstr(buf, "ERR ") != NULL);

    ok("auth client", auth_client(&s, cli, path));

    write(cli, "CONNECT 1\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("connect event", strstr(buf, "STATE connecting\n") != NULL);
    ok("apply got start", rec.last_kind == CTL_ACT_START);
    ok("apply got index", rec.last_index == 1);
    ok("apply got host", strcmp(rec.last_host, "2.2.2.2") == 0);

    char ev[64]; size_t en = 0;
    ctl_engine_notify(&s.engine, CTL_STATE_CONNECTED, ev, sizeof ev, &en);
    ctl_server_broadcast(&s, ev, en);
    exchange(&s, cli, buf, sizeof buf);
    ok("broadcast connected", strcmp(buf, "STATE connected\n") == 0);

    write(cli, "DISCONNECT\n", 11);
    exchange(&s, cli, buf, sizeof buf);
    ok("disconnect event", strcmp(buf, "STATE idle\n") == 0);
    ok("apply got stop", rec.last_kind == CTL_ACT_STOP);

    int calls_before = rec.calls;
    write(cli, "FLOOP\n", 6);
    exchange(&s, cli, buf, sizeof buf);
    ok("bad cmd err", strncmp(buf, "ERR ", 4) == 0);
    ok("bad cmd no apply", rec.calls == calls_before);

    write(cli, "CONNECT 99\n", 11);
    exchange(&s, cli, buf, sizeof buf);
    ok("connect bad idx err", strncmp(buf, "ERR ", 4) == 0);
    ok("connect bad idx no apply", rec.calls == calls_before);

    rec.fail_next = 8;
    write(cli, "CONNECT 0\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("start fail surfaces error", strstr(buf, "STATE error\n") != NULL);
    ok("failed failover keeps requested selection", s.engine.store.selected == 0);

    rec.fail_next = 0;
    g_verify_fail = 1;
    write(cli, "CONNECT 0\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("fallback connect succeeds", strstr(buf, "STATE connected\n") != NULL);
    ok("fallback keeps requested selection", s.engine.store.selected == 0);
    ok("fallback applies next server", rec.last_index == 1);

    write(cli, "DISCONNECT\n", 11);
    exchange(&s, cli, buf, sizeof buf);
    ok("fallback test disconnects", strcmp(buf, "STATE idle\n") == 0);

    /* refresh uses a canned two-server blob to avoid network flake */
    memset(&g_fetch, 0, sizeof g_fetch);
    g_fetch.blob =
        "vless://cccc3333-6324-4d53-ad4f-8cda48b30811@3.3.3.3:443?security=none&type=tcp#SubA\n"
        "vless://dddd4444-6324-4d53-ad4f-8cda48b30811@4.4.4.4:443?security=tls&type=ws#SubB\n";
    ctl_server_set_fetch(&s, mock_fetch);

    size_t sub;
    store_add_sub(&s.engine.store, "Home", "https://sub.example.com/feed", &sub);
    size_t n_before = s.engine.store.n;

    write(cli, "REFRESH 0\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("refresh ok reply", strncmp(buf, "OK ", 3) == 0);
    ok("fetch was called", g_fetch.calls == 1);
    ok("fetch got the sub url", strcmp(g_fetch.last_url, "https://sub.example.com/feed") == 0);
    ok("refresh added 2 servers", s.engine.store.n == n_before + 2);
    int sub_servers = 0;
    for (size_t i = 0; i < s.engine.store.n; ++i)
        if (s.engine.store.group[i] == (int)sub) sub_servers++;
    ok("two sub-group servers", sub_servers == 2);

    int fcalls = g_fetch.calls;
    write(cli, "REFRESH 9\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("refresh bad idx err", strncmp(buf, "ERR ", 4) == 0);
    ok("refresh bad idx no fetch", g_fetch.calls == fcalls);

    size_t n_now = s.engine.store.n;
    g_fetch.fail_next = 1;
    write(cli, "REFRESH 0\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("refresh fetch-fail err", strncmp(buf, "ERR ", 4) == 0);
    ok("refresh fetch-fail no change", s.engine.store.n == n_now);

    /* fetch streams arbitrary bodies as fdata/fdend */
    g_fetch.blob = "hello fetch";
    int fc_before = g_fetch.calls;
    {
        const char *fetch_cmd = "FETCH http://example.com/pkg\n";
        write(cli, fetch_cmd, strlen(fetch_cmd));
    }
    {
        char fbuf[4096];
        exchange(&s, cli, fbuf, sizeof fbuf);
        ok("fetch called", g_fetch.calls == fc_before + 1);
        ok("fetch got url", strcmp(g_fetch.last_url, "http://example.com/pkg") == 0);
        ok("fetch has fdata", strstr(fbuf, "FDATA ") != NULL);
        ok("fetch has fdend", strstr(fbuf, "FDEND 11\n") != NULL);

        unsigned char got[64];
        size_t glen = 0;
        const char *p = strstr(fbuf, "FDATA ");
        const char *nl = p ? strchr(p, '\n') : NULL;
        size_t blen = (p && nl) ? (size_t)(nl - (p + 6)) : 0;
        ok("fetch decode", p != NULL && blen > 0
            && b64_decode(p + 6, blen, got, sizeof got, &glen) == 0
            && glen == 11 && memcmp(got, "hello fetch", 11) == 0);
    }

    /* ping uses the direct hook only while idle */
    memset(&g_probe, 0, sizeof g_probe);
    memset(&g_tunnel_probe, 0, sizeof g_tunnel_probe);
    g_probe.ret_ms = 42;
    g_tunnel_probe.ret_ms = 77;
    ctl_server_set_probe(&s, mock_probe);
    ctl_server_set_tunnel_probe(&s, mock_tunnel_probe);

    write(cli, "PING 0\n", 7);
    exchange(&s, cli, buf, sizeof buf);
    ok("pong reply", strcmp(buf, "PONG 0 42\n") == 0);
    ok("probe called", g_probe.calls == 1);
    ok("probe got host", strcmp(g_probe.last_host, "1.1.1.1") == 0);
    ok("probe got port", g_probe.last_port == 443);

    g_probe.ret_ms = -1;
    write(cli, "PING 1\n", 7);
    exchange(&s, cli, buf, sizeof buf);
    ok("pong unreachable", strcmp(buf, "PONG 1 -1\n") == 0);

    int pcalls = g_probe.calls;
    write(cli, "PING 99\n", 8);
    exchange(&s, cli, buf, sizeof buf);
    ok("ping bad idx err", strncmp(buf, "ERR ", 4) == 0);
    ok("ping bad idx no probe", g_probe.calls == pcalls);

    store_select(&s.engine.store, 1);
    ctl_engine_notify(&s.engine, CTL_STATE_CONNECTED, ev, sizeof ev, &en);
    g_probe.ret_ms = 42;
    pcalls = g_probe.calls;
    int tcalls = g_tunnel_probe.calls;
    write(cli, "PING 0\n", 7);
    exchange(&s, cli, buf, sizeof buf);
    ok("ping connected uses tunnel", strcmp(buf, "PONG 0 77\n") == 0);
    ok("ping connected no probe", g_probe.calls == pcalls);

    write(cli, "PING 1\n", 7);
    exchange(&s, cli, buf, sizeof buf);
    ok("ping active uses tunnel", strcmp(buf, "PONG 1 77\n") == 0);
    ok("ping active uses tunnel probe", g_tunnel_probe.calls == tcalls + 2);
    ok("ping active no probe", g_probe.calls == pcalls);

    memset(&g_persist, 0, sizeof g_persist);
    ctl_server_set_persist(&s, mock_persist);
    rec.calls = 0;
    write(cli, "DELSRV 1\n", 9);
    exchange(&s, cli, buf, sizeof buf);
    ok("delsrv active state", strstr(buf, "STATE idle\n") != NULL);
    ok("delsrv active ok", strstr(buf, "OK removed server\n") != NULL);
    ok("delsrv active stop", rec.last_kind == CTL_ACT_STOP);
    ok("delsrv active persisted", g_persist.calls == 1 && g_persist.last_n == s.engine.store.n);

    close(cli);
    for (int i = 0; i < 10; ++i) ctl_server_step(&s, 2);
    ok("client reaped", ctl_server_client_count(&s) == 0);

    ctl_server_close(&s);

    if (g_fail) { fprintf(stderr, "%d check(s) failed\n", g_fail); return 1; }
    printf("all ctl_server checks passed\n");
    return 0;
}
