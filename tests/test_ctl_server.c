#define _DEFAULT_SOURCE

#include "ctl_server.h"
#include "senkod_helper.h"
#include "daemon_ctl.h"
#include "settings.h"
#include "core/b64.h"
#include "core/control.h"
#include "core/store.h"

#include <errno.h>
#include <stdlib.h>
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

/* the daemon side of the AWG verb */
static struct {
    char flag[32];
    char path[256];
    int busy;
} g_awg;

static int mock_awg(void *ctx, const char *flag, const char *path, char *out, size_t cap) {
    (void)ctx;
    snprintf(g_awg.flag, sizeof g_awg.flag, "%s", flag);
    snprintf(g_awg.path, sizeof g_awg.path, "%s", path ? path : "");
    if (strcmp(flag, "--awg-probe") == 0) return CTL_AWG_CHILD;
    snprintf(out, cap, "%s", strcmp(flag, "--awg") == 0 ? "connecting" : "connected");
    return 0;
}

static int mock_awg_busy(void *ctx) {
    (void)ctx;
    return g_awg.busy;
}

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
    const char *routing;    /* the routing response header the panel sends */
    int  routing_too_long;
    int  body_cut;
    size_t body_cap;
    int  hold_fd; /* when set, the fetch waits for a byte here like a slow panel */
} fetch_rec_t;

static fetch_rec_t g_fetch;

static int mock_fetch(void *ctx, const char *url,
                      const char *request_header,
                      unsigned char *buf, size_t cap, size_t *len,
                      ctl_fetch_meta_t *meta) {
    (void)ctx;
    g_fetch.body_cap = cap;
    if (g_fetch.hold_fd > 0) {
        char b;
        (void)read(g_fetch.hold_fd, &b, 1);
    }
    if (meta) {
        meta->expire = g_fetch.expire;
        meta->body_cut = g_fetch.body_cut;
        meta->gated = g_fetch.gated;
        snprintf(meta->routing, sizeof meta->routing, "%s", g_fetch.routing ? g_fetch.routing : "");
        meta->routing_too_long = g_fetch.routing_too_long;
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

static int g_native_fail;

static int mock_native_config(void *ctx, const vl_server_t *server,
                               char *buf, size_t cap, size_t *len) {
    (void)ctx;
    if (g_native_fail || !server || cap < 3) return -1;
    memcpy(buf, "{}", 3);
    *len = 2;
    return 0;
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
        if (reason && cap) snprintf(reason, cap, "no direct address table on this backend");
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

/* ---- routing profile fixtures: a profile as base64 and a geo dat to serve it ---- */

typedef struct { unsigned char *p; size_t n, cap; } pbuf_t;

static void pput(pbuf_t *b, const void *d, size_t n) {
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2 + 64; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}
static void pvarint(pbuf_t *b, uint64_t v) {
    unsigned char c;
    do { c = v & 0x7f; v >>= 7; if (v) c |= 0x80; pput(b, &c, 1); } while (v);
}
static void pfield(pbuf_t *b, unsigned f, const pbuf_t *m) {
    pvarint(b, f << 3 | 2); pvarint(b, m->n); pput(b, m->p, m->n);
}
static void pstr(pbuf_t *b, unsigned f, const char *t) {
    pvarint(b, f << 3 | 2); pvarint(b, strlen(t)); pput(b, t, strlen(t));
}

static int g_geo_fetches;
static int g_geo_fail;

/* serves a geosite or geoip dat that holds the tag "ru" */
static int mock_geo_fetch(void *ctx, const char *url, FILE *out, size_t max_bytes,
                          size_t *written, char *error, size_t error_cap) {
    (void)ctx; (void)max_bytes;
    ++g_geo_fetches;
    if (g_geo_fail) {
        snprintf(error, error_cap, "geo.example answered HTTP 503");
        return -1;
    }
    pbuf_t dat = {0}, e = {0}, d = {0};
    pstr(&e, 1, "RU");
    if (strstr(url, "geosite")) {
        pvarint(&d, 1 << 3); pvarint(&d, 2);
        pstr(&d, 2, "yandex.ru");
        pfield(&e, 2, &d);
    } else {
        const unsigned char net[4] = { 8, 8, 8, 0 };
        pvarint(&d, 1 << 3 | 2); pvarint(&d, 4); pput(&d, net, 4);
        pvarint(&d, 2 << 3); pvarint(&d, 24);
        pfield(&e, 2, &d);
    }
    pfield(&dat, 1, &e);
    fwrite(dat.p, 1, dat.n, out);
    *written = dat.n;
    free(dat.p); free(e.p); free(d.p);
    return 0;
}

static void routing_link(const char *verb, const char *json, char *out, size_t cap) {
    size_t n = strlen(json), len = 0;
    char b64[2048];
    b64_encode((const unsigned char *)json, n, b64, sizeof b64, &len);
    b64[len] = '\0';
    snprintf(out, cap, "happ://routing/%s/%s", verb, b64);
}

/* every line of a streamed answer, up to and including the one that starts with end */
static size_t stream_until(ctl_server_t *s, int cli, const char *end, char *out, size_t cap) {
    size_t tot = 0;
    for (int round = 0; round < 40; ++round) {
        for (int i = 0; i < 20; ++i) ctl_server_step(s, 2);
        for (;;) {
            ssize_t n = read(cli, out + tot, cap - 1 - tot);
            if (n <= 0) break;
            tot += (size_t)n;
        }
        out[tot] = '\0';
        if (strstr(out, end)) return tot;
    }
    return tot;
}

static void send_line(int fd, const char *line) {
    if (write(fd, line, strlen(line)) < 0) perror("write");
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
    static ctl_server_t blocked;
    ok("regular file blocks init",
       ctl_server_init(&blocked, block_path, mock_apply, NULL) == CTLS_ERR_BIND);
    struct stat bst;
    ok("regular file preserved", stat(block_path, &bst) == 0 && S_ISREG(bst.st_mode));
    unlink(block_path);

    const char *replace_path = "/tmp/senko_ctltest.replace";
    unlink(replace_path);
    static ctl_server_t replaced;
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

    static ctl_server_t s;
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
    ok("parse the real delay check",
       ctl_parse_cmd("CHECK real 3 stages\n", 20, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_CHECK && parsed.server_index == 3 &&
       strcmp(parsed.name, "real") == 0 && parsed.want_stages == 1);
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
    /* the root work the app used to run through a setuid helper */
    static const char awg_start[] = "AWG START /var/mobile/Library/Preferences/Senko/a.conf\n";
    ok("parse amneziawg start with its config",
       ctl_parse_cmd(awg_start, sizeof awg_start - 1, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_AWG && strcmp(parsed.name, "--awg") == 0 &&
       strcmp(parsed.text, "/var/mobile/Library/Preferences/Senko/a.conf") == 0);
    ok("parse amneziawg stop without a path",
       ctl_parse_cmd("AWG STOP\n", 9, &parsed) == CTL_OK &&
       strcmp(parsed.name, "--awg-stop") == 0 && parsed.text[0] == '\0');
    ok("amneziawg start needs a path",
       ctl_parse_cmd("AWG START\n", 10, &parsed) == CTL_ERR_PARSE);
    ok("amneziawg status takes no path",
       ctl_parse_cmd("AWG STATUS /x\n", 14, &parsed) == CTL_ERR_PARSE);
    ok("an unknown amneziawg verb is refused",
       ctl_parse_cmd("AWG REBOOT\n", 11, &parsed) == CTL_ERR_PARSE);
    ok("parse a package update",
       ctl_parse_cmd("UPDATE /tmp/senko.deb\n", 22, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_UPDATE && strcmp(parsed.text, "/tmp/senko.deb") == 0);
    ok("an update needs a path", ctl_parse_cmd("UPDATE\n", 7, &parsed) == CTL_ERR_PARSE);
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
/* senkod --update treats this exact line as proof that a daemon is listening, so the
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

    write(cli, "AWG STATUS\n", 11);
    exchange(&s, cli, buf, sizeof buf);
    ok("amneziawg without a daemon hook is refused",
       strcmp(buf, "ERR amneziawg is not available\n") == 0);

    ctl_server_set_awg(&s, mock_awg, mock_awg_busy);
    write(cli, "AWG STATUS\n", 11);
    exchange(&s, cli, buf, sizeof buf);
    ok("an amneziawg status comes back as one AWG line and its end",
       strcmp(buf, "AWG connected\nAWGEND 0 0\n") == 0 &&
       strcmp(g_awg.flag, "--awg-status") == 0);

    ctl_engine_notify(&s.engine, CTL_STATE_CONNECTED, ev, sizeof ev, &en);
    rec.last_kind = CTL_ACT_NONE;
    static const char awg_start_line[] =
        "AWG START /var/mobile/Library/Preferences/Senko/a.conf\n";
    write(cli, awg_start_line, sizeof awg_start_line - 1);
    exchange(&s, cli, buf, sizeof buf);
    ok("starting amneziawg takes a running vless tunnel down first",
       rec.last_kind == CTL_ACT_STOP && s.engine.state == CTL_STATE_IDLE &&
       strstr(buf, "STATE idle") != NULL && strstr(buf, "AWG connecting\nAWGEND 0 0\n") != NULL);
    ok("the profile path reaches the daemon",
       strcmp(g_awg.flag, "--awg") == 0 &&
       strcmp(g_awg.path, "/var/mobile/Library/Preferences/Senko/a.conf") == 0);

    g_awg.busy = 1;
    write(cli, "CONNECT 0\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("vless does not start under a running amneziawg tunnel",
       strcmp(buf, "ERR amneziawg active\n") == 0);
    g_awg.busy = 0;

    /* the host has no installed senkod to run in a helper mode */
    if (access(senkod_binary_path(), X_OK) != 0) {
        static const char probe_line[] =
            "AWG PROBE /var/mobile/Library/Preferences/Senko/a.conf\n";
        write(cli, probe_line, sizeof probe_line - 1);
        exchange(&s, cli, buf, sizeof buf);
        ok("a probe that cannot start its child says which binary it needed",
           strncmp(buf, "AWG error ", 10) == 0 && strstr(buf, senkod_binary_path()) != NULL &&
           strstr(buf, "\nAWGEND 1 0\n") != NULL);
        write(cli, "UPDATE /tmp/senko.deb\n", 22);
        exchange(&s, cli, buf, sizeof buf);
        ok("an update names the helper it could not run",
           strncmp(buf, "ERR ", 4) == 0 && strstr(buf, "cannot be run") != NULL);
    }

    ctl_server_set_awg(&s, NULL, NULL);

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
    g_verify_reason = "server: the server did not return an answer: check the UUID; also check whether the server can reach the internet";
    g_verify_fail = 1;
    write(cli, "CONNECT 0\n", 10);
    exchange(&s, cli, buf, sizeof buf);
    ok("server diagnosis keeps its layer and complete reason",
       strstr(buf, "ERR server: the server did not return an answer: check the UUID; also check whether the server can reach the internet\n") != NULL);
    ok("server diagnosis has no duplicate prefix", strstr(buf, "server: server:") == NULL);
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

    {
        const char *saved_blob = g_fetch.blob;
        size_t before = s.engine.store.n;
        g_fetch.body_cut = 1;
        write(cli, "REFRESH 0\n", 10);
        exchange(&s, cli, buf, sizeof buf);
        ok("a cut subscription is refused", strncmp(buf, "ERR ", 4) == 0 &&
           strstr(buf, "too large") != NULL);
        ok("a cut subscription keeps every saved server", s.engine.store.n == before);
        g_fetch.body_cut = 0;

        size_t padding = 600 * 1024;
        size_t tail_cap = 2048;
        const char head[] = "[{\"remarks\":\"large\",\"padding\":\"";
        size_t head_len = sizeof head - 1;
        char *large = malloc(head_len + padding + tail_cap);
        ok("allocate large feed", large != NULL);
        if (large) {
            memcpy(large, head, head_len);
            memset(large + head_len, 'x', padding);
            snprintf(large + head_len + padding, tail_cap,
                "\",\"outbounds\":[{\"protocol\":\"vless\",\"settings\":{\"address\":\"late.example\","
                "\"port\":443,\"id\":\"11111111-1111-4111-8111-111111111111\",\"encryption\":\"none\"}}]}]");
            g_fetch.blob = large;
            write(cli, "REFRESH 0\n", 10);
            exchange(&s, cli, buf, sizeof buf);
            ok("a node after 512 KB is imported", strncmp(buf, "OK ", 3) == 0 &&
               strcmp(s.engine.store.servers[s.engine.store.n - 1].host, "late.example") == 0);
            ok("subscription download stays bounded", g_fetch.body_cap == 8 * 1024 * 1024);
            g_fetch.blob = saved_blob;
            write(cli, "REFRESH 0\n", 10);
            exchange(&s, cli, buf, sizeof buf);
            ok("restore feed after the large download", strncmp(buf, "OK ", 3) == 0 &&
               s.engine.store.n == before);
            free(large);
        }
    }

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

/* a happ panel's routing profile: stored with the subscription, switched on by
   onadd, listed, and turned into geo downloads without blocking the socket */
    {
        static char big[65536];
        char link[2200], geo_dir[64];
        const char *profile =
            "{\"Name\":\"Mixed\",\"GlobalProxy\":\"false\","
            "\"DirectSites\":[\"geosite:ru\",\"domain:direct.test\",\"regexp:^x\"],"
            "\"DirectIp\":[\"geoip:ru\"],\"ProxySites\":[\"domain:via.test\"],"
            "\"Geoipurl\":\"https://geo.example/geoip.dat\","
            "\"Geositeurl\":\"https://geo.example/geosite.dat\"}";
        snprintf(geo_dir, sizeof geo_dir, "/tmp/senko-ctlgeo-%d", (int)getpid());
        ctl_server_set_geo(&s, mock_geo_fetch, geo_dir);
        g_geo_fetches = 0;
        g_fetch.hold_fd = 0;

        routing_link("onadd", profile, link, sizeof link);
        g_fetch.routing = link;
        send_line(cli, "REFRESH 0\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("a refresh that carries routing reports it",
           strncmp(buf, "OK refreshed", 12) == 0 && strstr(buf, "routing profile added and switched on") != NULL);
        ok("the profile is stored and switched on",
           s.engine.store.routing_active == 0 && strcmp(store_sub_routing(&s.engine.store, 0), profile) == 0);
        for (int i = 0; i < 200 && ctl_server_geo_busy(&s); ++i) {
            ctl_server_step(&s, 5);
            usleep(5000);
        }
        ok("the geo data was downloaded once per dat", !ctl_server_geo_busy(&s) && g_geo_fetches == 2);

        send_line(cli, "GEO\n");
        stream_until(&s, cli, "GEO ", big, sizeof big);
        ok("geo state after the download", strstr(big, "GEO 0 ") != NULL && strstr(big, " 0 1 ") != NULL);
        send_line(cli, "ROUTING\n");
        stream_until(&s, cli, "ROUTINGEND", big, sizeof big);
        ok("routing lists the profile", strstr(big, "ROUTING 0 1 0 ") != NULL && strstr(big, " Mixed\n") != NULL &&
           strstr(big, "ROUTINGEND 0\n") != NULL);
        ok("routing counts what senko can match", strstr(big, "ROUTING 0 1 0 4 1 2 ") != NULL);

        send_line(cli, "ROUTINGENTRIES 0\n");
        stream_until(&s, cli, "RENTRYEND", big, sizeof big);
        ok("entries list", strstr(big, "RENTRY direct site 1 geosite:ru\n") != NULL &&
           strstr(big, "RENTRY direct site 0 regexp:^x\n") != NULL &&
           strstr(big, "RENTRY proxy site 1 domain:via.test\n") != NULL &&
           strstr(big, "RENTRY direct ip 1 geoip:ru\n") != NULL && strstr(big, "RENTRYEND 5\n") != NULL);
        send_line(cli, "ROUTINGENTRIES 7\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("entries of a subscription without a profile", strncmp(buf, "ERR ", 4) == 0);

        /* the same profile again changes nothing and does not download again */
        send_line(cli, "REFRESH 0\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("an unchanged profile is quiet", strncmp(buf, "OK refreshed", 12) == 0 && strstr(buf, "routing") == NULL &&
           g_geo_fetches == 2);

        /* switching routing off and on */
        send_line(cli, "ROUTINGSET off\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("routing off", strncmp(buf, "OK routing is off", 17) == 0 && s.engine.store.routing_active == -1);
        send_line(cli, "ROUTINGSET 0\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("routing on", strncmp(buf, "OK routing profile switched on", 30) == 0 && s.engine.store.routing_active == 0);
        send_line(cli, "ROUTINGSET 5\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("a subscription without a profile cannot be switched on", strncmp(buf, "ERR ", 4) == 0 &&
           s.engine.store.routing_active == 0);
        send_line(cli, "ROUTINGSET x\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("a bad index is refused", strncmp(buf, "ERR ", 4) == 0);

        /* GEOUPDATE forces a download; a failed one is reported, not hidden */
        g_geo_fail = 1;
        send_line(cli, "GEOUPDATE\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("geo update starts", strncmp(buf, "OK geo download started", 23) == 0);
        for (int i = 0; i < 200 && ctl_server_geo_busy(&s); ++i) {
            ctl_server_step(&s, 5);
            usleep(5000);
        }
        send_line(cli, "GEO\n");
        stream_until(&s, cli, "GEO ", big, sizeof big);
        ok("a failed download is reported", strstr(big, "GEO 0 ") != NULL && strstr(big, " 2 geo.example answered HTTP 503\n") != NULL);
        g_geo_fail = 0;

        /* a profile's rules never appear as the user's, and cannot be deleted */
        send_line(cli, "SET rule block domain-suffix mine.test\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("user rule saved", strncmp(buf, "OK rule saved", 13) == 0);
        store_apply_routing(&s.engine.store, NULL);
        ok("profile rules joined the ruleset", s.engine.store.rules.count > 1);
        send_line(cli, "RULES\n");
        stream_until(&s, cli, "RULEEND", big, sizeof big);
        ok("only the user's rule is listed", strstr(big, "mine.test") != NULL && strstr(big, "direct.test") == NULL &&
           strstr(big, "RULEEND 1\n") != NULL);
        send_line(cli, "DELRULE 1\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("a profile rule cannot be deleted by index", strncmp(buf, "ERR no such rule", 16) == 0);
        ruleset_init(&s.engine.store.rules);

        /* what cannot be used is reported and leaves the profile alone */
        g_fetch.routing = "happ://routing/onadd/@@@@";
        send_line(cli, "REFRESH 0\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("a broken routing link is reported", strncmp(buf, "OK refreshed", 12) == 0 &&
           strstr(buf, "routing profile refused") != NULL && strcmp(store_sub_routing(&s.engine.store, 0), profile) == 0);
        g_fetch.routing = NULL;
        g_fetch.routing_too_long = 1;
        send_line(cli, "REFRESH 0\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("a routing header past the buffer is reported", strstr(buf, "too long") != NULL);
        g_fetch.routing_too_long = 0;

        /* the panel withdraws its profile */
        g_fetch.routing = "happ://routing/off";
        send_line(cli, "REFRESH 0\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("off withdraws the profile", strstr(buf, "switched its routing off") != NULL &&
           s.engine.store.routing_active == -1 && store_sub_routing(&s.engine.store, 0)[0] == '\0');
        g_fetch.routing = NULL;
        {
            char cmd[128];
            snprintf(cmd, sizeof cmd, "rm -rf %s", geo_dir);
            if (system(cmd) != 0) ok("geo dir cleanup", 0);
        }
    }

/* a panel that takes seconds to answer used to stall the whole control
   socket, so the app saw "senkod did not answer" for every other request */
    {
        int hold[2];
        ok("hold pipe", pipe(hold) == 0);
        g_fetch.hold_fd = hold[0];
        write(cli, "REFRESH 0\n", 10);
        for (int i = 0; i < 20; ++i) ctl_server_step(&s, 2);
        write(cli, "STATUS\n", 7);
        exchange(&s, cli, buf, sizeof buf);
        ok("status answers while a refresh downloads",
           strncmp(buf, "STATE ", 6) == 0 && strstr(buf, "refreshed") == NULL);
        write(cli, "REFRESH 0\n", 10);
        exchange(&s, cli, buf, sizeof buf);
        ok("a second refresh of the same subscription waits its turn",
           strstr(buf, "ERR refresh in progress") != NULL);
        (void)write(hold[1], "x", 1);
        exchange(&s, cli, buf, sizeof buf);
        ok("the slow refresh still answers", strncmp(buf, "OK refreshed", 12) == 0);

        size_t kept = s.engine.store.n;
        write(cli, "REFRESH 0\n", 10);
        for (int i = 0; i < 20; ++i) ctl_server_step(&s, 2);
        {
            const char *cmd = "REPLACESUB 0 https://moved.example/feed - Home\n";
            write(cli, cmd, strlen(cmd));
        }
        exchange(&s, cli, buf, sizeof buf);
        ok("the subscription is replaced mid refresh", strncmp(buf, "OK ", 3) == 0);
        (void)write(hold[1], "x", 1);
        exchange(&s, cli, buf, sizeof buf);
        ok("a replaced subscription does not take the old feed",
           strstr(buf, "ERR the subscription was changed during the refresh") != NULL &&
           s.engine.store.n == kept);
        g_fetch.hold_fd = 0;
        close(hold[0]);
        close(hold[1]);
        {
            const char *cmd =
                "REPLACESUB 0 https://sub.example.com/feed Authorization%3A%20Bearer%20abc%2Btest Home%20updated\n";
            write(cli, cmd, strlen(cmd));
        }
        exchange(&s, cli, buf, sizeof buf);
        ok("the subscription is restored", strncmp(buf, "OK ", 3) == 0);
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
    {
        char first[65] = {0}, renamed[65] = {0}, changed[65] = {0};
        char *record = strstr(buf, "SRVID 0 ");
        ok("list carries a stable server identity", record && sscanf(record, "SRVID 0 %64s", first) == 1 &&
           strlen(first) == 64);
        vl_server_t original = s.engine.store.servers[0];
        snprintf(s.engine.store.servers[0].remark, sizeof original.remark, "renamed");
        write(cli, "LIST\n", 5);
        exchange(&s, cli, buf, sizeof buf);
        record = strstr(buf, "SRVID 0 ");
        ok("a renamed node keeps its measurements", record &&
           sscanf(record, "SRVID 0 %64s", renamed) == 1 && strcmp(first, renamed) == 0);
        snprintf(s.engine.store.servers[0].path, sizeof original.path, "/different");
        write(cli, "LIST\n", 5);
        exchange(&s, cli, buf, sizeof buf);
        record = strstr(buf, "SRVID 0 ");
        ok("a different path at the same endpoint has its own identity", record &&
           sscanf(record, "SRVID 0 %64s", changed) == 1 && strcmp(first, changed) != 0);
        s.engine.store.servers[0] = original;
    }

    {
        int selected = s.engine.store.selected;
        int saves = g_persist.calls;
        ctl_persist_fn saved_persist = s.persist;
        ctl_server_set_persist(&s, mock_persist);
        ctl_server_set_native_config(&s, mock_native_config);
        send_line(cli, "NATIVE_CONFIG 1\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("native config has a complete reply", strstr(buf, "NCFGEND 2\n") != NULL);
        ok("native VPN remembers the chosen server", s.engine.store.selected == 1 &&
           g_persist.calls == saves + 1);
        g_native_fail = 1;
        send_line(cli, "NATIVE_CONFIG 0\n");
        exchange(&s, cli, buf, sizeof buf);
        ok("a failed native config keeps selection", strncmp(buf, "ERR ", 4) == 0 &&
           s.engine.store.selected == 1 && g_persist.calls == saves + 1);
        g_native_fail = 0;
        store_select(&s.engine.store, selected);
        ctl_server_set_persist(&s, saved_persist);
    }

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
/* the main poll sleeps until the next real deadline: the stats beat only while
   an authed client listens, a redial when one is due, otherwise no timeout */
    struct pollfd idle_pfd[CTL_SERVER_POLL_MAX];
    int idle_wait = -1;
    ok("prepare fills the poll set",
       ctl_server_prepare(&s, idle_pfd, CTL_SERVER_POLL_MAX, &idle_wait) >= 2);
    ok("an authed stats client bounds the wait to the next sample",
       idle_wait >= 0 && idle_wait <= 1000);
    ok("prepare refuses a short poll set",
       ctl_server_prepare(&s, idle_pfd, CTL_SERVER_POLL_MAX - 1, &idle_wait) == 0);
    ctl_server_set_stats(&s, NULL);
    const daemon_settings_t *kept_settings = s.settings;
    long kept_retry = s.retry_at_ms;
    s.settings = NULL;
    s.retry_at_ms = 0;
    idle_wait = -1;
    ctl_server_prepare(&s, idle_pfd, CTL_SERVER_POLL_MAX, &idle_wait);
    ok("an idle control server asks for no timeout", idle_wait == -1);
    s.retry_at_ms = 1;
    ctl_server_prepare(&s, idle_pfd, CTL_SERVER_POLL_MAX, &idle_wait);
    ok("an overdue redial wakes at once", idle_wait == 0);
    s.settings = kept_settings;
    s.retry_at_ms = kept_retry;
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

    ok("parse routing", ctl_parse_cmd("ROUTING\n", 8, &parsed) == CTL_OK && parsed.kind == CTL_CMD_ROUTING);
    ok("routing takes no argument", ctl_parse_cmd("ROUTING 1\n", 10, &parsed) != CTL_OK);
    ok("parse routing set", ctl_parse_cmd("ROUTINGSET 4\n", 13, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_ROUTING_SET && parsed.server_index == 4);
    ok("parse routing off", ctl_parse_cmd("ROUTINGSET off\n", 15, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_ROUTING_SET && parsed.server_index == -1);
    ok("routing set needs an index", ctl_parse_cmd("ROUTINGSET\n", 11, &parsed) != CTL_OK &&
       ctl_parse_cmd("ROUTINGSET -2\n", 14, &parsed) != CTL_OK &&
       ctl_parse_cmd("ROUTINGSET x\n", 13, &parsed) != CTL_OK &&
       ctl_parse_cmd("ROUTINGSET OFF\n", 15, &parsed) != CTL_OK);
    ok("parse routing entries", ctl_parse_cmd("ROUTINGENTRIES 2\n", 17, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_ROUTING_ENTRIES && parsed.server_index == 2 &&
       ctl_parse_cmd("ROUTINGENTRIES\n", 15, &parsed) != CTL_OK);
    ok("parse geo", ctl_parse_cmd("GEO\n", 4, &parsed) == CTL_OK && parsed.kind == CTL_CMD_GEO &&
       ctl_parse_cmd("GEOUPDATE\n", 10, &parsed) == CTL_OK && parsed.kind == CTL_CMD_GEO_UPDATE &&
       ctl_parse_cmd("GEO now\n", 8, &parsed) != CTL_OK);
    ok("parse vless-core removal", ctl_parse_cmd("VLESSCORE REMOVE\n", 17, &parsed) == CTL_OK &&
       parsed.kind == CTL_CMD_RIVAL_REMOVE &&
       ctl_parse_cmd("VLESSCORE\n", 10, &parsed) != CTL_OK &&
       ctl_parse_cmd("VLESSCORE REMOVE ALL\n", 21, &parsed) != CTL_OK &&
       ctl_parse_cmd("VLESSCORE remove\n", 17, &parsed) != CTL_OK);
    {
        char line[256];
        size_t n = 0;
        ok("build routing", ctl_build_routing(3, 1, 0, 12, 2, 4, 1700000000ULL, "Mixed profile", line,
                                              sizeof line, &n) == CTL_OK &&
           strcmp(line, "ROUTING 3 1 0 12 2 4 1700000000 Mixed profile\n") == 0);
        ok("build routing end", ctl_build_routingend(-1, line, sizeof line, &n) == CTL_OK &&
           strcmp(line, "ROUTINGEND -1\n") == 0);
        ok("build entry", ctl_build_rentry("direct", 0, 1, "geosite:ru", line, sizeof line, &n) == CTL_OK &&
           strcmp(line, "RENTRY direct site 1 geosite:ru\n") == 0 &&
           ctl_build_rentry("block", 1, 0, "geoip:x", line, sizeof line, &n) == CTL_OK &&
           strcmp(line, "RENTRY block ip 0 geoip:x\n") == 0);
        ok("build entry end", ctl_build_rentryend(7, line, sizeof line, &n) == CTL_OK &&
           strcmp(line, "RENTRYEND 7\n") == 0);
        ok("build geo", ctl_build_geo(1, 99, 3, 2, "it failed", line, sizeof line, &n) == CTL_OK &&
           strcmp(line, "GEO 1 99 3 2 it failed\n") == 0 &&
           ctl_build_geo(0, 0, 0, 0, NULL, line, sizeof line, &n) == CTL_OK && strcmp(line, "GEO 0 0 0 0 \n") == 0);
        ok("build routing too small", ctl_build_routing(0, 0, 0, 0, 0, 0, 0, "x", line, 8, &n) == CTL_ERR_BUF);
    }

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

/* the egress is sampled only for a tunnel that could be rebuilt, so an idle
   senkod with auto_reconnect on still sleeps without a timeout */
    ctl_state_t kept_state = s.engine.state;
    s.retry_at_ms = 0;
    s.engine.state = CTL_STATE_IDLE;
    snprintf(s.egress_iface, sizeof s.egress_iface, "en0");
    s.egress_check_ms = 1;
    ctl_server_tick(&s);
    ok("an idle tunnel forgets the egress baseline",
       s.egress_iface[0] == '\0' && s.egress_check_ms == 0);
    idle_wait = -1;
    ctl_server_prepare(&s, idle_pfd, CTL_SERVER_POLL_MAX, &idle_wait);
    ok("an idle tunnel does not sample the egress", idle_wait == -1);
    s.engine.state = CTL_STATE_CONNECTED;
    idle_wait = -1;
    ctl_server_prepare(&s, idle_pfd, CTL_SERVER_POLL_MAX, &idle_wait);
    ok("a live tunnel takes its first egress sample at once", idle_wait == 0);
    s.engine.state = kept_state;

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
       strstr(buf, "ERR no direct address table on this backend") != NULL);

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

/* the app's status poll, three pings and a refresh are open at once; a full
   table has to say so instead of hanging up without a word */
    {
        int held[CTL_SERVER_MAX_CLIENTS];
        for (int i = 0; i < CTL_SERVER_MAX_CLIENTS; ++i) {
            held[i] = connect_unix(path);
            for (int k = 0; k < 3; ++k) ctl_server_step(&s, 2);
        }
        ok("the app's usual burst fits", ctl_server_client_count(&s) == CTL_SERVER_MAX_CLIENTS);
        int extra = connect_unix(path);
        for (int k = 0; k < 5; ++k) ctl_server_step(&s, 2);
        ssize_t en = extra >= 0 ? read(extra, buf, sizeof buf - 1) : -1;
        if (en >= 0) buf[en] = '\0';
        ok("a connection past the cap is refused with a reason",
           en > 0 && strcmp(buf, "ERR too many control connections\n") == 0);
        if (extra >= 0) close(extra);
        for (int i = 0; i < CTL_SERVER_MAX_CLIENTS; ++i)
            if (held[i] >= 0) close(held[i]);
        for (int i = 0; i < 20; ++i) ctl_server_step(&s, 2);
        ok("held clients reaped", ctl_server_client_count(&s) == 0);
    }

    ctl_server_close(&s);

    if (g_fail) { fprintf(stderr, "%d check(s) failed\n", g_fail); return 1; }
    printf("all ctl_server checks passed\n");
    return 0;
}
