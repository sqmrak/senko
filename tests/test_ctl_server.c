#define _DEFAULT_SOURCE

#include "ctl_server.h"
#include "daemon_ctl.h"
#include "settings.h"
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
static int g_stats_calls = 0;
static int g_stats_fail = 0;
static int sample_stats(void *ctx, uint64_t *up, uint64_t *down) {
    (void)ctx;
    ++g_stats_calls;
    *up = UINT64_C(4294967300);
    *down = UINT64_C(8589934600);
    return g_stats_fail ? -1 : 0;
}
static const char *g_verify_reason = "mock verify failed";
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
    int               fail_next;    /* fail this many starts */
} apply_rec_t;

/* the daemon owns the settings copy, so the mock applies a SET the same way */
static daemon_settings_t *g_settings;

static int mock_apply(void *ctx, const ctl_action_t *a) {
    apply_rec_t *r = (apply_rec_t *)ctx;
    r->calls++;
    r->last_kind = a->kind;
    r->last_index = a->server_index;
    snprintf(r->last_host, sizeof r->last_host, "%s", a->server.host);
    if (a->kind == CTL_ACT_SET) {
        if (!g_settings) return -1;
        settings_status_t sr = daemon_settings_set(g_settings, a->key, strlen(a->key),
                                                   a->value, strlen(a->value));
        if (sr == SETTINGS_ERR_KEY) return DCTL_ERR_SETTING_KEY;
        if (sr != SETTINGS_OK) return DCTL_ERR_SETTING_VALUE;
        return 0;
    }
    if (r->fail_next > 0) { r->fail_next--; return -1; }
    return 0;
}

/* fixed fetch hook */
typedef struct {
    int  calls;
    char last_url[512];
    char last_header[512];
    const char *blob;
    int  fail_next;
    uint64_t expire;
    int  gated;
    const char *gate_reason;
} fetch_rec_t;

static fetch_rec_t g_fetch;

static int mock_fetch(void *ctx, const char *url,
                      const char *request_header,
                      unsigned char *buf, size_t cap, size_t *len,
                      ctl_fetch_meta_t *meta) {
    (void)ctx;
    if (meta) {
        meta->expire = g_fetch.expire;
        meta->gated = g_fetch.gated;
        snprintf(meta->gate_reason, sizeof meta->gate_reason, "%s",
                 g_fetch.gate_reason ? g_fetch.gate_reason : "");
    }
    g_fetch.calls++;
    snprintf(g_fetch.last_url, sizeof g_fetch.last_url, "%s", url);
    snprintf(g_fetch.last_header, sizeof g_fetch.last_header, "%s",
             request_header ? request_header : "");
    if (g_fetch.fail_next) { g_fetch.fail_next = 0; return -1; }
    if (!g_fetch.blob) return -1;
    size_t bl = strlen(g_fetch.blob);
    if (bl > cap) return -1;
    memcpy(buf, g_fetch.blob, bl);
    *len = bl;
    return 0;
}

/* fixed probe hook */
typedef struct {
    int  calls;
    char last_host[256];
    uint16_t last_port;
    int  ret_ms;        /* returned rtt */
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
            snprintf(reason, reason_cap, "%s", g_verify_reason);
        return -1;
    }
    if (reason && reason_cap) reason[0] = '\0';
    return 0;
}

/* the daemon side of the diagnostics: the server adds its own facts first and
   appends whatever the hook wrote */
static int g_diag_calls;

static int mock_diag(void *ctx, char *buf, size_t cap, size_t *len) {
    (void)ctx;
    g_diag_calls++;
    return ctl_build_diag("mock.key", "mock value", buf, cap, len) == CTL_OK ? 0 : -1;
}

/* the firewall ruleset the kernel is running, and the named state drops */
static int g_fwconf_calls;

static int mock_fwconf(void *ctx, char *buf, size_t cap, size_t *len) {
    (void)ctx;
    g_fwconf_calls++;
    int n = snprintf(buf, cap, "rdr on en0 proto tcp to any -> 127.0.0.1 port 1\n"
                               "pass out quick proto tcp\n");
    if (n < 0 || (size_t)n >= cap) return -1;
    if (len) *len = (size_t)n;
    return 0;
}

static char g_flush_what[32];

static int mock_flush(void *ctx, const char *what, char *reason, size_t cap) {
    (void)ctx;
    snprintf(g_flush_what, sizeof g_flush_what, "%s", what ? what : "");
    if (what && strcmp(what, "bypass") == 0) {
        if (reason && cap) snprintf(reason, cap, "no pf bypass table on this backend");
        return -1;
    }
    return 0;
}

/* the stage trace the developer screen asks for with the optional keyword */
static int g_check_stage_requests;

static int mock_check_staged(void *ctx, const char *mode,
                             const vl_server_t *server,
                             ctl_check_trace_t *trace,
                             char *reason, size_t reason_cap) {
    (void)ctx; (void)mode; (void)server;
    if (reason && reason_cap) reason[0] = '\0';
    if (!trace) return 7;
    g_check_stage_requests++;
    memset(trace, 0, sizeof *trace);
    snprintf(trace->stages[0].name, sizeof trace->stages[0].name, "resolve");
    trace->stages[0].ms = 3;
    trace->stages[0].ok = 1;
    snprintf(trace->stages[1].name, sizeof trace->stages[1].name, "tcp connect");
    trace->stages[1].ms = 21;
    trace->stages[1].ok = 0;
    trace->count = 2;
    if (reason && reason_cap) snprintf(reason, reason_cap, "check failed");
    return -1;
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

/* drain replies after a few loop ticks */
/* the server queues a reply and flushes it on a later step, so one pump can
   return before the line reaches the client and the read comes back empty.
   every caller here waits for a whole line, and the wait is bounded so a reply
   that never arrives still fails its own assertion */
/* a state line carries the tunnel age as a trailing token once the clock has
   moved, so a test that compared the whole line failed whenever the run
   straddled a second */
static int state_line_is(const char *buf, const char *name) {
    size_t n = strlen(name);
    if (strncmp(buf, "STATE ", 6) != 0) return 0;
    if (strncmp(buf + 6, name, n) != 0) return 0;
    const char *p = buf + 6 + n;
    if (*p == ' ') {
        ++p;
        if (*p < '0' || *p > '9') return 0;
        while (*p >= '0' && *p <= '9') ++p;
    }
    return p[0] == '\n' && p[1] == '\0';
}

static size_t exchange(ctl_server_t *s, int cli, char *out, size_t cap) {
    size_t tot = 0;
    for (int round = 0; round < 25; ++round) {
        for (int i = 0; i < 20; ++i) ctl_server_step(s, 2);
        for (;;) {
            ssize_t n = read(cli, out + tot, cap - 1 - tot);
            if (n <= 0) break;
            tot += (size_t)n;
        }
        out[tot] = '\0';
        if (tot > 0 && out[tot - 1] == '\n') return tot;
    }
    out[tot] = '\0';
    return tot;
}

/* test the auth gate */
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
    struct stat sockst;
    ok("control socket is not world accessible",
       stat(path, &sockst) == 0 && (sockst.st_mode & 0007) == 0);
    char token_path[160];
    token_path_from_sock(path, token_path, sizeof token_path);
    ok("control token is not world accessible",
       stat(token_path, &sockst) == 0 && (sockst.st_mode & 0007) == 0);
    ctl_cmd_t parsed;
    ok("parse typed check",
       ctl_parse_cmd("CHECK handshake 1\n", 18, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_CHECK && parsed.server_index == 1 &&
       strcmp(parsed.name, "handshake") == 0);
    ok("reject unknown check",
       ctl_parse_cmd("CHECK magic 1\n", 14, &parsed) == CTL_ERR_PARSE);
    ok("a check without the keyword asks for no stages",
       ctl_parse_cmd("CHECK tcp 0\n", 12, &parsed) == CTL_OK &&
       parsed.server_index == 0 && parsed.want_stages == 0);
    ok("the stages keyword is read off the end",
       ctl_parse_cmd("CHECK tcp 0 stages\n", 19, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_CHECK && parsed.server_index == 0 &&
       strcmp(parsed.name, "tcp") == 0 && parsed.want_stages == 1);
    ok("parse native vpn configuration",
       ctl_parse_cmd("NATIVE_CONFIG 4\n", 16, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_NATIVE_CONFIG && parsed.server_index == 4);
    ok("reject native vpn configuration without index",
       ctl_parse_cmd("NATIVE_CONFIG\n", 14, &parsed) == CTL_ERR_PARSE);
    ok("the keyword is not mistaken for an index",
       ctl_parse_cmd("CHECK tcp stages\n", 17, &parsed) == CTL_ERR_PARSE);
    ok("parse the firewall dump",
       ctl_parse_cmd("FWCONF\n", 7, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_FWCONF);
    ok("parse a named flush",
       ctl_parse_cmd("FLUSH dns\n", 10, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_FLUSH && strcmp(parsed.name, "dns") == 0);
    ok("reject a flush with no target",
       ctl_parse_cmd("FLUSH\n", 6, &parsed) == CTL_ERR_PARSE);
    ok("reject a flush target this build does not own",
       ctl_parse_cmd("FLUSH everything\n", 17, &parsed) == CTL_ERR_PARSE);
    ok("parse a device id reset",
       ctl_parse_cmd("HWIDRESET\n", 10, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_HWID_RESET);
    ok("parse an atomic manual replacement",
       ctl_parse_cmd("REPLACESRV 3 vless://example\n", 29, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_REPLACE_SERVER && parsed.server_index == 3 &&
       strcmp(parsed.text, "vless://example") == 0);
    ok("parse an atomic subscription replacement",
       ctl_parse_cmd("REPLACESUB 2 https://sub.example - Updated\n", 43, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_REPLACE_SUB && parsed.server_index == 2 &&
       strcmp(parsed.text, "https://sub.example") == 0 &&
       strcmp(parsed.value, "-") == 0 && strcmp(parsed.name, "Updated") == 0);
    {
        char line[128];
        size_t ln = 0;
        ok("a stage line carries the verdict, the time and the name",
           ctl_build_stage("tcp connect", 21, 0, line, sizeof line, &ln) == CTL_OK &&
           strcmp(line, "STAGE 0 21 tcp connect\n") == 0);
        ok("a stage name with a newline in it is refused",
           ctl_build_stage("bad\nname", 1, 1, line, sizeof line, &ln) == CTL_ERR_ARG);
        ok("a firewall line is sent verbatim",
           ctl_build_fwline("pass out quick", line, sizeof line, &ln) == CTL_OK &&
           strcmp(line, "FWLINE pass out quick\n") == 0);
    }
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

    char buf[8192];

    write(cli, "STATUS\n", 7);
    exchange(&s, cli, buf, sizeof buf);
/* senko-kick treats this exact line as proof that a daemon is listening, so the
   wording is part of the control contract, not just a message */
    ok("status needs auth", strcmp(buf, "ERR auth required\n") == 0);

    write(cli, "CONNECT 1\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("connect needs auth", strstr(buf, "auth required") != NULL ||
                            strstr(buf, "ERR ") != NULL);

    ok("auth client", auth_client(&s, cli, path));

    write(cli, "STATUS\n", 7);
    exchange(&s, cli, buf, sizeof buf);
    ok("status idle authed", state_line_is(buf, "idle"));

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
    ok("broadcast connected", state_line_is(buf, "connected"));

    write(cli, "DISCONNECT\n", 11);
    exchange(&s, cli, buf, sizeof buf);
    ok("disconnect event", state_line_is(buf, "idle"));
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
    ok("failed connect keeps requested selection", s.engine.store.selected == 0);

    rec.fail_next = 0;
    rec.last_index = -1;
    g_verify_fail = 1;
    write(cli, "CONNECT 0\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("strict connect reports failure", strstr(buf, "STATE error\n") != NULL);
    ok("strict connect keeps requested selection", s.engine.store.selected == 0);
    ok("strict connect does not switch server", rec.last_index == 0);

    g_verify_reason = "routing rules accepted but traffic was not redirected";
    g_verify_fail = 1;
    write(cli, "CONNECT 0\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("routing verification has routing layer",
       strstr(buf, "ERR routing: routing rules accepted but traffic was not redirected\n") != NULL);
    ok("routing verification never reports connected",
       strstr(buf, "STATE connected\n") == NULL);
    ok("routing verification removes active routing", rec.last_kind == CTL_ACT_STOP);
    g_verify_reason = "mock verify failed";

    write(cli, "DISCONNECT\n", 11);
    exchange(&s, cli, buf, sizeof buf);
    ok("strict test disconnects", state_line_is(buf, "idle"));

    /* use a fixed refresh body */
    memset(&g_fetch, 0, sizeof g_fetch);
    g_fetch.blob =
        "vless://cccc3333-6324-4d53-ad4f-8cda48b30811@3.3.3.3:443?security=none&type=tcp#SubA\n"
        "vless://dddd4444-6324-4d53-ad4f-8cda48b30811@4.4.4.4:443?security=tls&type=ws#SubB\n";
    g_fetch.expire = 1893456000ULL;
    ctl_server_set_fetch(&s, mock_fetch);

    size_t sub;
    store_add_sub(&s.engine.store, "Home", "https://sub.example.com/feed", &sub);
    size_t n_before = s.engine.store.n;

    write(cli, "REFRESH 0\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("refresh ok reply", strncmp(buf, "OK ", 3) == 0);
    ok("fetch was called", g_fetch.calls == 1);
    ok("fetch got the sub url", strcmp(g_fetch.last_url, "https://sub.example.com/feed") == 0);
    ok("refresh has no header by default", g_fetch.last_header[0] == '\0');
    ok("refresh added 2 servers", s.engine.store.n == n_before + 2);
    ok("refresh stores subscription expiry",
       s.engine.store.subs[sub].expire == 1893456000ULL);
    int sub_servers = 0;
    for (size_t i = 0; i < s.engine.store.n; ++i)
        if (s.engine.store.group[i] == (int)sub) sub_servers++;
    ok("two sub-group servers", sub_servers == 2);

    int fcalls = g_fetch.calls;
    write(cli, "REFRESH 9\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("refresh bad idx err", strncmp(buf, "ERR ", 4) == 0);
    ok("refresh bad idx no fetch", g_fetch.calls == fcalls);

    {
        const char *cmd = "SETSUBHDR 0 Authorization%3A%20Bearer%20abc%2Btest\n";
        write(cli, cmd, strlen(cmd));
    }
    exchange(&s, cli, buf, sizeof buf);
    ok("subscription header saved", strncmp(buf, "OK ", 3) == 0);
    write(cli, "REFRESH 0\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("refresh with header", strncmp(buf, "OK ", 3) == 0 &&
       strcmp(g_fetch.last_header, "Authorization: Bearer abc+test") == 0);

    size_t sub_nodes_before_replace = s.engine.store.n;
    {
        const char *cmd =
            "REPLACESUB 0 https://sub.example.com/feed Authorization%3A%20Bearer%20abc%2Btest Home%20updated\n";
        write(cli, cmd, strlen(cmd));
    }
    exchange(&s, cli, buf, sizeof buf);
    ok("replace subscription reply", strcmp(buf, "OK subscription updated\n") == 0);
    ok("replace subscription keeps nodes", s.engine.store.n == sub_nodes_before_replace &&
       strcmp(s.engine.store.subs[0].name, "Home%20updated") == 0 &&
       strcmp(s.engine.store.subs[0].header, "Authorization: Bearer abc+test") == 0);

    size_t n_now = s.engine.store.n;
    g_fetch.fail_next = 1;
    write(cli, "REFRESH 0\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("refresh fetch-fail err", strncmp(buf, "ERR ", 4) == 0);
    ok("refresh fetch-fail no change", s.engine.store.n == n_now);

/* a panel that refuses the device answers 200 with a one entry placeholder
   profile; replacing the saved nodes with it is what made every row show the
   same name, so the refresh has to fail and keep what is stored */
    {
        const char *saved_blob = g_fetch.blob;
        g_fetch.gated = 1;
        g_fetch.gate_reason = "device limit reached";
        g_fetch.blob =
            "vless://00000000-0000-0000-0000-000000000000@0.0.0.0:1"
            "?encryption=none&type=tcp&security=none#App%20not%20supported\n";
        write(cli, "REFRESH 0\n", 10);
        exchange(&s, cli, buf, sizeof buf);
        ok("gated refresh errs", strncmp(buf, "ERR ", 4) == 0);
        ok("gated refresh names the reason",
           strstr(buf, "device limit reached") != NULL);
        ok("gated refresh keeps the servers", s.engine.store.n == n_now);
        g_fetch.gated = 0;
        g_fetch.gate_reason = NULL;
        g_fetch.blob = saved_blob;
    }

    {
        const char *cmd = "ADDSUB https://second.example/feed Second\n";
        write(cli, cmd, strlen(cmd));
    }
    exchange(&s, cli, buf, sizeof buf);
    ok("add second subscription", strncmp(buf, "OK ", 3) == 0);
    {
        const char *cmd = "MOVESECTION 1 0\n";
        write(cli, cmd, strlen(cmd));
    }
    exchange(&s, cli, buf, sizeof buf);
    ok("move section reply", strcmp(buf, "OK section moved\n") == 0);
    ok("move section persisted", s.engine.store.section_order[0] == 1);

    {
        const char *cmd =
            "ADDSRV vless://eeee5555-6324-4d53-ad4f-8cda48b30811@5.5.5.5:443?security=none&type=tcp#Manual2\n";
        write(cli, cmd, strlen(cmd));
    }
    exchange(&s, cli, buf, sizeof buf);
    ok("add second manual server", strncmp(buf, "OK ", 3) == 0);
    int manual_idx = (int)s.engine.store.n - 1;
    {
        char cmd[64];
        int n = snprintf(cmd, sizeof cmd, "MOVEMANUAL %d 0\n", manual_idx);
        write(cli, cmd, (size_t)n);
    }
    exchange(&s, cli, buf, sizeof buf);
    ok("move manual reply", strcmp(buf, "OK server moved\n") == 0);
    ok("move manual changed order", strcmp(s.engine.store.servers[0].host, "5.5.5.5") == 0);

    {
        const char *cmd =
            "REPLACESRV 0 vless://ffff5555-6324-4d53-ad4f-8cda48b30811@6.6.6.6:443?security=none&type=tcp#Edited\n";
        write(cli, cmd, strlen(cmd));
    }
    exchange(&s, cli, buf, sizeof buf);
    ok("replace manual reply", strcmp(buf, "OK server updated\n") == 0);
    ok("replace manual keeps its position",
       s.engine.store.n > 0 && strcmp(s.engine.store.servers[0].host, "6.6.6.6") == 0);

    write(cli, "LIST\n", 5);
    exchange(&s, cli, buf, sizeof buf);
    ok("list has subscription metadata", strstr(buf, "SUBMETA 0 1893456000\n") != NULL);
    ok("list has subscription info", strstr(buf, "SUBINFO 0 ") != NULL);
    ok("list has subscription header", strstr(buf, "SUBHDR 0 Authorization:%20Bearer%20abc%2Btest\n") != NULL);
    ok("list has section order", strstr(buf, "SECTION 1 -1 0\n") != NULL);

    /* test fetch streaming */
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

    /* test idle ping */
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
    ok("probe got host", strcmp(g_probe.last_host, "6.6.6.6") == 0);
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
    ok("ping connected probes selected server", strcmp(buf, "PONG 0 42\n") == 0);
    ok("ping connected uses server probe", g_probe.calls == pcalls + 1);

    write(cli, "PING 1\n", 7);
    exchange(&s, cli, buf, sizeof buf);
    ok("ping active probes selected server", strcmp(buf, "PONG 1 42\n") == 0);
    ok("ping active does not use tunnel probe", g_tunnel_probe.calls == tcalls);
    ok("ping active uses server probe", g_probe.calls == pcalls + 2);

    memset(&g_persist, 0, sizeof g_persist);
    ctl_server_set_persist(&s, mock_persist);
    rec.calls = 0;
    write(cli, "DELSRV 1\n", 9);
    exchange(&s, cli, buf, sizeof buf);
    ok("delsrv active state", strstr(buf, "STATE idle\n") != NULL);
    ok("delsrv active ok", strstr(buf, "OK removed server\n") != NULL);
    ok("delsrv active stop", rec.last_kind == CTL_ACT_STOP);
    ok("delsrv active persisted", g_persist.calls == 1 && g_persist.last_n == s.engine.store.n);

    int unauth = connect_unix(path);
    ok("stats unauth connect", unauth >= 0);
    set_nonblock(unauth);
    ctl_server_step(&s, 0);
    ctl_server_set_stats(&s, sample_stats);
    ctl_server_step(&s, 0);
    ssize_t stat_n = read(cli, buf, sizeof buf - 1);
    if (stat_n >= 0) buf[stat_n] = '\0';
    ok("stats broadcast", stat_n > 0 && strcmp(buf, "STAT 4294967300 8589934600\n") == 0);
    ok("stats auth gate", read(unauth, buf, sizeof buf) < 0 && errno == EAGAIN);
    int samples = g_stats_calls;
    ctl_server_step(&s, 0);
    ok("stats once per second", g_stats_calls == samples);
    s.stat_at_ms -= 1000;
    ctl_server_step(&s, 0);
    ok("stats idle poll", g_stats_calls == samples + 1);
    ok("stats idle delivery", read(cli, buf, sizeof buf) > 0);
    write(cli, "STATUS\n", 7);
    exchange(&s, cli, buf, sizeof buf);
    ok("stats before status", strncmp(buf, "STAT 4294967300 8589934600\nSTATE ", 31) == 0);
    g_stats_fail = 1;
    write(cli, "STATUS\n", 7);
    exchange(&s, cli, buf, sizeof buf);
    ok("stats failure preserves state", strncmp(buf, "STATE ", 6) == 0 && s.stat_failed);
    g_stats_fail = 0;
    write(cli, "STATUS\n", 7);
    exchange(&s, cli, buf, sizeof buf);
    ok("stats recovery", strstr(buf, "STAT ") != NULL && !s.stat_failed);
    ctl_server_set_stats(&s, NULL);
    close(unauth);

/* the settings verbs: the daemon owns the values, so SET goes out as an action
   the apply hook performs and SETTINGS answers from the same copy */
    ok("parse set",
       ctl_parse_cmd("SET auto_connect 1\n", 19, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_SET && strcmp(parsed.name, "auto_connect") == 0 &&
       strcmp(parsed.text, "1") == 0);
/* a key this build has no field for still parses: the daemon is what names it
   unknown, and the parser must not turn it into a different verb */
    ok("parse set unknown key",
       ctl_parse_cmd("SET quantum_tunnel 1\n", 21, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_SET && strcmp(parsed.name, "quantum_tunnel") == 0);
    ok("parse set needs a value",
       ctl_parse_cmd("SET auto_connect\n", 17, &parsed) == CTL_ERR_PARSE);
    ok("parse settings", ctl_parse_cmd("SETTINGS\n", 9, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_SETTINGS);
/* SETSUBHDR must not be read as a SET of a key called SUBHDR */
    ok("set does not swallow setsubhdr",
       ctl_parse_cmd("SETSUBHDR 0 x\n", 14, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_SET_SUB_HEADER);
    ok("parse rules",
       ctl_parse_cmd("RULES\n", 6, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_RULES);
    ok("parse delrule",
       ctl_parse_cmd("DELRULE 3\n", 10, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_DEL_RULE && parsed.server_index == 3);

    write(cli, "SETTINGS\n", 9);
    exchange(&s, cli, buf, sizeof buf);
    ok("settings without a daemon copy", strncmp(buf, "ERR ", 4) == 0);

    daemon_settings_t live;
    daemon_settings_defaults(&live);
    g_settings = &live;
    ctl_server_set_settings(&s, &live);

    write(cli, "SETTINGS\n", 9);
    exchange(&s, cli, buf, sizeof buf);
    ok("settings dump ends", strstr(buf, "SETEND\n") != NULL);
    ok("settings dump carries defaults",
       strstr(buf, "SET auto_reconnect 1\n") != NULL &&
       strstr(buf, "SET sub_refresh_hours 0\n") != NULL);

    memset(&g_persist, 0, sizeof g_persist);
    write(cli, "SET auto_connect 1\n", 19);
    exchange(&s, cli, buf, sizeof buf);
    ok("set applied", strcmp(buf, "OK auto_connect 1\n") == 0);
    ok("set reached the daemon copy", live.auto_connect == 1);
    ok("set persisted", g_persist.calls == 1);

    write(cli, "SET quantum_tunnel 1\n", 21);
    exchange(&s, cli, buf, sizeof buf);
    ok("unknown setting reported", strcmp(buf, "ERR unknown setting\n") == 0);

    write(cli, "SET sub_refresh_hours 999\n", 26);
    exchange(&s, cli, buf, sizeof buf);
    ok("out of range setting reported", strcmp(buf, "ERR value out of range\n") == 0);
    ok("out of range setting changed nothing", live.sub_refresh_hours == 0);

    write(cli, "SETTINGS\n", 9);
    exchange(&s, cli, buf, sizeof buf);
    ok("settings dump follows the change",
       strstr(buf, "SET auto_connect 1\n") != NULL);

    memset(&g_persist, 0, sizeof g_persist);
    {
        const char *cmd = "SET rule direct domain-suffix example.org\n";
        write(cli, cmd, strlen(cmd));
    }
    exchange(&s, cli, buf, sizeof buf);
    ok("rule saved through control", strncmp(buf, "OK rule saved ", 14) == 0 &&
       s.engine.store.rules.count == 1 && g_persist.calls == 1);
    write(cli, "RULES\n", 6);
    exchange(&s, cli, buf, sizeof buf);
    ok("rule list streams hits",
       strstr(buf, "RULE 0 direct domain-suffix 0 example.org\n") != NULL &&
       strstr(buf, "RULEEND 1\n") != NULL);
    s.engine.state = CTL_STATE_CONNECTED;
    {
        const char *cmd = "SET rule block domain-keyword ads\n";
        write(cli, cmd, strlen(cmd));
    }
    exchange(&s, cli, buf, sizeof buf);
    ok("live rule change requires reconnect",
       strcmp(buf, "ERR disconnect before changing rules\n") == 0 &&
       s.engine.store.rules.count == 1);
    s.engine.state = CTL_STATE_IDLE;
    write(cli, "DELRULE 0\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("rule removed through control", strcmp(buf, "OK rule removed\n") == 0 &&
       s.engine.store.rules.count == 0);

/* a redial the daemon scheduled on its own must not fight the user: a command
   that decides what the tunnel does now cancels it */
    s.retry_at_ms = 1;
    s.retry_attempts = 3;
    write(cli, "DISCONNECT\n", 11);
    exchange(&s, cli, buf, sizeof buf);
    ok("disconnect cancels a pending redial",
       s.retry_at_ms == 0 && s.retry_attempts == 0);

    ctl_server_set_settings(&s, NULL);
    g_settings = NULL;

/* the diagnostics report: the parser takes a bare verb, the server always
   answers something, and a key or value that would read back as two facts is
   refused by the builder rather than written */
    ok("parse diag", ctl_parse_cmd("DIAG\n", 5, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_DIAG);
    char diagline[64]; size_t dn = 0;
    ok("diag builder refuses a key with a space",
       ctl_build_diag("two words", "x", diagline, sizeof diagline, &dn) == CTL_ERR_ARG);
    ok("diag builder refuses a value with a newline",
       ctl_build_diag("key", "two\nlines", diagline, sizeof diagline, &dn) == CTL_ERR_ARG);
    ok("diag builder keeps an empty value readable",
       ctl_build_diag("key", "", diagline, sizeof diagline, &dn) == CTL_OK &&
       strcmp(diagline, "DIAG key -\n") == 0);

    write(cli, "DIAG\n", 5);
    exchange(&s, cli, buf, sizeof buf);
    ok("diag answers without a daemon hook",
       strstr(buf, "DIAG state ") != NULL && strstr(buf, "DIAGEND\n") != NULL);
    ok("diag reports the catalog", strstr(buf, "DIAG catalog ") != NULL);
    ok("diag names the selected server", strstr(buf, "DIAG server ") != NULL);
/* the report is five taps away and ends up in screenshots */
    ok("diag carries no uuid", strstr(buf, "-6324-4d53-") == NULL);

    g_diag_calls = 0;
    ctl_server_set_diag(&s, mock_diag);
    write(cli, "DIAG\n", 5);
    exchange(&s, cli, buf, sizeof buf);
    ok("diag asked the daemon", g_diag_calls == 1);
    ok("diag appended the daemon facts",
       strstr(buf, "DIAG mock.key mock value\n") != NULL);
    ctl_server_set_diag(&s, NULL);

    write(cli, "FWCONF\n", 7);
    exchange(&s, cli, buf, sizeof buf);
    ok("no firewall backend says so instead of showing an empty ruleset",
       strstr(buf, "ERR ") != NULL);
    ctl_server_set_fwconf(&s, mock_fwconf);
    g_fwconf_calls = 0;
    write(cli, "FWCONF\n", 7);
    exchange(&s, cli, buf, sizeof buf);
    ok("the firewall ruleset goes out line by line",
       g_fwconf_calls == 1 &&
       strstr(buf, "FWLINE rdr on en0 proto tcp to any -> 127.0.0.1 port 1\n") != NULL &&
       strstr(buf, "FWLINE pass out quick proto tcp\n") != NULL &&
       strstr(buf, "FWEND\n") != NULL);

    ctl_server_set_flush(&s, mock_flush);
    g_flush_what[0] = '\0';
    write(cli, "FLUSH dns\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("a dns flush reaches the daemon",
       strcmp(g_flush_what, "dns") == 0 && strstr(buf, "OK flushed") != NULL);
    write(cli, "FLUSH bypass\n", 13);
    exchange(&s, cli, buf, sizeof buf);
    ok("a refused flush answers with the daemon's own words",
       strstr(buf, "ERR no pf bypass table on this backend") != NULL);

    g_flush_what[0] = '\0';
    {
        const char rule[] = "direct domain-suffix example.com";
        size_t rule_index = 0;
        ok("a rule exists to remove",
           ruleset_add_text(&s.engine.store.rules, rule, sizeof rule - 1,
                            &rule_index) == RULES_OK &&
           s.engine.store.rules.count > 0);
    }
    write(cli, "FLUSH rules\n", 12);
    exchange(&s, cli, buf, sizeof buf);
    ok("the ruleset is cleared by the server, not the daemon",
       g_flush_what[0] == '\0' && s.engine.store.rules.count == 0 &&
       strstr(buf, "rule(s) removed") != NULL);
    ctl_server_set_flush(&s, NULL);
    ctl_server_set_fwconf(&s, NULL);

    ctl_server_set_check(&s, mock_check_staged);
    g_check_stage_requests = 0;
    write(cli, "CHECK tcp 0\n", 12);
    exchange(&s, cli, buf, sizeof buf);
    ok("a check that did not ask for stages gets none",
       g_check_stage_requests == 0 && strstr(buf, "STAGE ") == NULL &&
       strstr(buf, "PONG ") != NULL);
    write(cli, "CHECK tcp 0 stages\n", 19);
    exchange(&s, cli, buf, sizeof buf);
    ok("a staged check reports every stage before the verdict",
       g_check_stage_requests == 1 &&
       strstr(buf, "STAGE 1 3 resolve\n") != NULL &&
       strstr(buf, "STAGE 0 21 tcp connect\n") != NULL &&
       strstr(buf, "ERR check failed") != NULL);
    ctl_server_set_check(&s, NULL);

    close(cli);
    for (int i = 0; i < 10; ++i) ctl_server_step(&s, 2);
    ok("client reaped", ctl_server_client_count(&s) == 0);

    ctl_server_close(&s);

    if (g_fail) { fprintf(stderr, "%d check(s) failed\n", g_fail); return 1; }
    printf("all ctl_server checks passed\n");
    return 0;
}
