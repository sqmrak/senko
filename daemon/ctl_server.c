#define _DEFAULT_SOURCE

#include "ctl_server.h"
#include "core/b64.h"
#include "core/control.h"
#include "daemon_ctl.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <grp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef S_ISSOCK
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#endif

#define AWG_PID_PATH "/var/run/senkoawgd.pid"

static int awg_tunnel_running(void) {
    FILE *f = fopen(AWG_PID_PATH, "r");
    long pid = 0;
    int ok = f && fscanf(f, "%ld", &pid) == 1 && pid > 1 && kill((pid_t)pid, 0) == 0;
    if (f) fclose(f);
    return ok;
}

static void set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static int remove_stale_socket(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0)
        return (errno == ENOENT) ? 0 : -1;
    if (!S_ISSOCK(st.st_mode))
        return -1;
    return unlink(path);
}

/* keep token next to sock so mobile can open it without a separate path table */
static void ctl_token_path_from_sock(const char *sock, char *out, size_t cap) {
    if (!sock || !out || cap < 8) {
        if (out && cap) out[0] = '\0';
        return;
    }
    size_t n = strlen(sock);
    if (n >= 5 && strcmp(sock + n - 5, ".sock") == 0) {
        size_t base = n - 5;
        if (base + 6 >= cap) { out[0] = '\0'; return; }
        memcpy(out, sock, base);
        memcpy(out + base, ".token", 7);
        return;
    }
    if (n + 6 >= cap) { out[0] = '\0'; return; }
    memcpy(out, sock, n);
    memcpy(out + n, ".token", 7);
}

#define CTL_MOBILE_GID 501 /* stock jb mobile gid; chown needs the number not the name */

static int write_ctl_token(const char *path, char *token_out, size_t token_cap) {
    if (!path || !token_out || token_cap < 33) return -1;
    unsigned char raw[16];
    int ur = open("/dev/urandom", O_RDONLY);
    ssize_t got = (ur >= 0) ? read(ur, raw, sizeof raw) : -1;
    if (ur >= 0) close(ur);
    if (got != (ssize_t)sizeof raw) {
        pid_t pid = getpid();
        for (size_t i = 0; i < sizeof raw; ++i)
            raw[i] = (unsigned char)((pid * 131u) ^ (unsigned)(i * 17u) ^ 0x5au);
    }
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof raw; ++i) {
        token_out[i * 2]     = hex[raw[i] >> 4];
        token_out[i * 2 + 1] = hex[raw[i] & 0xf];
    }
    token_out[32] = '\0';

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0640);
    if (fd < 0) return -1;
    char line[48];
    int ln = snprintf(line, sizeof line, "%s\n", token_out);
    ssize_t w = (ln > 0) ? write(fd, line, (size_t)ln) : -1;
    (void)fchmod(fd, 0640);
    close(fd);
    if (w != (ssize_t)ln) {
        unlink(path);
        return -1;
    }
    (void)chown(path, 0, CTL_MOBILE_GID);
    return 0;
}

static void tighten_sock_perms(const char *path) {
    if (!path || !path[0]) return;
    if (chown(path, 0, CTL_MOBILE_GID) == 0)
        chmod(path, 0660);
    else
        chmod(path, 0666); /* host/test builds lack mobile; else ui cannot connect */
}

ctls_status_t ctl_server_init(ctl_server_t *s, const char *path,
                              ctl_apply_fn apply, void *apply_ctx) {
    if (!s || !path) return CTLS_ERR_ARG;
    memset(s, 0, sizeof *s);
    s->listen_fd = -1;
    for (size_t i = 0; i < CTL_SERVER_MAX_CLIENTS; ++i) s->clients[i].fd = -1;
    s->apply = apply;
    s->apply_ctx = apply_ctx;
    ctl_engine_init(&s->engine);

    size_t pl = strlen(path);
    if (pl >= sizeof s->sock_path) return CTLS_ERR_ARG;
    memcpy(s->sock_path, path, pl + 1);
    ctl_token_path_from_sock(path, s->token_path, sizeof s->token_path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return CTLS_ERR_BIND;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, pl + 1);

    if (remove_stale_socket(path) != 0) {
        close(fd);
        return CTLS_ERR_BIND;
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(fd);
        return CTLS_ERR_BIND;
    }
    tighten_sock_perms(path);
    if (listen(fd, 4) != 0) {
        close(fd);
        unlink(path);
        return CTLS_ERR_BIND;
    }
    s->require_auth = (write_ctl_token(s->token_path, s->token, sizeof s->token) == 0);
    set_nonblock(fd);
    s->listen_fd = fd;
    return CTLS_OK;
}

void ctl_server_set_persist(ctl_server_t *s, ctl_persist_fn persist) {
    if (!s) return;
    s->persist = persist;
}

void ctl_server_set_fetch(ctl_server_t *s, ctl_fetch_fn fetch) {
    if (!s) return;
    s->fetch = fetch;
}

void ctl_server_set_probe(ctl_server_t *s, ctl_probe_fn probe) {
    if (!s) return;
    s->probe = probe;
}

void ctl_server_set_verify(ctl_server_t *s, ctl_verify_fn verify) {
    if (!s) return;
    s->verify = verify;
}

void ctl_server_set_tunnel_probe(ctl_server_t *s, ctl_tunnel_probe_fn probe) {
    if (!s) return;
    s->tunnel_probe = probe;
}

static ctl_client_t *alloc_client(ctl_server_t *s) {
    for (size_t i = 0; i < CTL_SERVER_MAX_CLIENTS; ++i)
        if (s->clients[i].fd < 0) return &s->clients[i];
    return NULL;
}

static void drop_client(ctl_client_t *c) {
    if (c->fd >= 0) close(c->fd);
    free(c->outbuf);
    c->fd = -1;
    c->authed = 0;
    c->in_len = 0;
    c->outbuf = NULL;
    c->out_len = 0;
    c->out_off = 0;
}

static int ctl_peer_allowed(int fd) {
#if defined(__APPLE__)
    uid_t uid = (uid_t)-1;
    gid_t gid = 0;
    if (getpeereid(fd, &uid, &gid) != 0) return 0;
    return uid == 0 || uid == 501;
#else
    (void)fd;
    return 1;
#endif
}

static void accept_one(ctl_server_t *s) {
    int cfd = accept(s->listen_fd, NULL, NULL);
    if (cfd < 0) return;
    if (!ctl_peer_allowed(cfd)) {
        close(cfd);
        return;
    }
    ctl_client_t *c = alloc_client(s);
    if (!c) { close(cfd); return; }
    set_nonblock(cfd);
    c->fd = cfd;
    c->authed = s->require_auth ? 0 : 1;
    c->in_len = 0;
}

static int client_flush(ctl_client_t *c) {
    while (c->out_off < c->out_len) {
        ssize_t w = write(c->fd, c->outbuf + c->out_off, c->out_len - c->out_off);
        if (w > 0) { c->out_off += (size_t)w; continue; }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        drop_client(c);
        return -1;
    }
    if (c->out_off == c->out_len) {
        c->out_off = 0;
        c->out_len = 0;
    }
    return 0;
}

static void client_write(ctl_client_t *c, const char *buf, size_t len) {
    if (!c || c->fd < 0 || !buf || len == 0) return;
    if (c->out_off > 0 && c->out_off == c->out_len) {
        c->out_off = 0;
        c->out_len = 0;
    } else if (c->out_off > 0) {
        memmove(c->outbuf, c->outbuf + c->out_off, c->out_len - c->out_off);
        c->out_len -= c->out_off;
        c->out_off = 0;
    }
    if (len > CTL_CLIENT_OUT_MAX - c->out_len) {
        drop_client(c);
        return;
    }
    if (c->out_len == 0) {
        ssize_t w = write(c->fd, buf, len);
        if (w == (ssize_t)len) return;
        if (w > 0) {
            buf += w;
            len -= (size_t)w;
        } else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            drop_client(c);
            return;
        }
    }
    if (len == 0) return;
    if (!c->outbuf) {
        c->outbuf = (char *)malloc(CTL_CLIENT_OUT_MAX);
        if (!c->outbuf) { drop_client(c); return; }
    }
    memcpy(c->outbuf + c->out_len, buf, len);
    c->out_len += len;
    (void)client_flush(c);
}

static const char *server_proto_name(const vl_server_t *sv) {
    if (sv->proto == VL_PROTO_SOCKS5) return "socks5";
    if (sv->proto == VL_PROTO_HTTP)   return "http";
    if (sv->proto == VL_PROTO_HTTPS)  return "https";
    return "vless";
}

static const char *server_net_name(const vl_server_t *sv) {
    if (sv->proto != VL_PROTO_VLESS) return "tcp";
    switch (sv->net) {
        case VL_NET_TCP:   return "tcp";
        case VL_NET_WS:    return "ws";
        case VL_NET_GRPC:  return "grpc";
        case VL_NET_HTTP:  return "http";
        case VL_NET_XHTTP: return "xhttp";
        default:          return "unknown";
    }
}

#define FETCH_CHUNK_RAW 384
#define FETCH_BODY_MAX (512 * 1024)

static int fetch_body_alloc(ctl_server_t *s, const char *url,
                            unsigned char **body_out, size_t *len_out) {
    if (!s || !s->fetch || !url || !body_out || !len_out) return -1;
    *body_out = NULL;
    *len_out = 0;
    unsigned char *body = (unsigned char *)malloc(FETCH_BODY_MAX);
    if (!body) return -1;
    size_t len = 0;
    if (s->fetch(s->apply_ctx, url, body, FETCH_BODY_MAX, &len) != 0 ||
        len > FETCH_BODY_MAX) {
        free(body);
        return -1;
    }
    *body_out = body;
    *len_out = len;
    return 0;
}

static void fetch_reply_body(ctl_client_t *c,
                             const unsigned char *body, size_t blen) {
    char line[520];
    size_t off = 0;
    while (off < blen) {
        size_t chunk = blen - off;
        if (chunk > FETCH_CHUNK_RAW) chunk = FETCH_CHUNK_RAW;
        char b64[520];
        size_t b64n = 0;
        if (b64_encode(body + off, chunk, b64, sizeof b64, &b64n) != 0) {
            char err[64]; size_t en = 0;
            if (ctl_build_err("fetch encode failed", err, sizeof err, &en) == CTL_OK)
                client_write(c, err, en);
            return;
        }
        int ln = snprintf(line, sizeof line, "FDATA %s\n", b64);
        if (ln > 0) client_write(c, line, (size_t)ln);
        off += chunk;
    }
    int ln = snprintf(line, sizeof line, "FDEND %zu\n", blen);
    if (ln > 0) client_write(c, line, (size_t)ln);
}

static int server_supported(const vl_server_t *sv);

/* cap tries so total verify time stays near the ui connect timeout */
#define CONNECT_FAILOVER_MAX 4

typedef struct {
    char layer[24];
    char reason[120];
} connect_failure_t;

static void connect_failure_set(connect_failure_t *f,
                                const char *layer,
                                const char *reason) {
    if (!f) return;
    snprintf(f->layer, sizeof f->layer, "%s", layer ? layer : "server");
    snprintf(f->reason, sizeof f->reason, "%s", reason ? reason : "unknown error");
}

static void connect_failure_from_apply(connect_failure_t *f, int r) {
    if (r == DCTL_ERR_TRANSPORT)
        connect_failure_set(f, "server", "unsupported protocol or security");
    else if (r == DCTL_ERR_UUID)
        connect_failure_set(f, "server", "bad uuid in server link");
    else if (r == DCTL_ERR_LOOP)
        connect_failure_set(f, "socks", "socks listener failed");
    else if (r == DCTL_ERR_DNS)
        connect_failure_set(f, "server", "dns resolution failed");
    else if (r == DCTL_ERR_ROUTING)
        connect_failure_set(f, "routing", "routing setup failed");
    else
        connect_failure_set(f, "server", "unknown error");
}

static void connect_failure_write(ctl_client_t *c, const connect_failure_t *f) {
    if (!c || !f || !f->reason[0]) return;
    char msg[160];
    snprintf(msg, sizeof msg, "%s: %s", f->layer[0] ? f->layer : "server", f->reason);
    char errbuf[192]; size_t errn = 0;
    if (ctl_build_err(msg, errbuf, sizeof errbuf, &errn) == CTL_OK)
        client_write(c, errbuf, errn);
}

static void connect_fail_notify_error(ctl_server_t *s, ctl_client_t *c, int r) {
    connect_failure_t f;
    connect_failure_from_apply(&f, r);
    if (c) {
        connect_failure_write(c, &f);
        char ev[64]; size_t en = 0;
        if (ctl_engine_notify(&s->engine, CTL_STATE_ERROR, ev, sizeof ev, &en) == CTL_OK)
            client_write(c, ev, en);
    } else {
        char ev[64]; size_t en = 0;
        (void)ctl_engine_notify(&s->engine, CTL_STATE_ERROR, ev, sizeof ev, &en);
    }
}

/* true if the ui still holds this control socket */
static int client_still_open(const ctl_client_t *c) {
    if (!c || c->fd < 0) return 0;
    struct pollfd p;
    p.fd = c->fd;
    p.events = 0;
    p.revents = 0;
    if (poll(&p, 1, 0) < 0) return 0;
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) return 0;
    return 1;
}

/* fail over until one server passes the complete socks and http probe */
static int connect_with_tunnel_pick(ctl_server_t *s, ctl_client_t *c, int start_idx) {
    if (!s || !s->apply) return -1;
    store_t *st = &s->engine.store;
    if (!st->n) return -1;

    size_t base = (start_idx >= 0 && (size_t)start_idx < st->n) ? (size_t)start_idx : 0;
    size_t tries = st->n < CONNECT_FAILOVER_MAX ? st->n : CONNECT_FAILOVER_MAX;
    int orig_sel = st->selected;
    int requested_sel = (int)base;
    connect_failure_t last_fail;
    connect_failure_set(&last_fail, "server", "no working server");

/* tell ui we started so a later timeout is not a silent nil reply */
    if (c) {
        char ev[64]; size_t en = 0;
        s->engine.state = CTL_STATE_CONNECTING;
        if (ctl_build_state(CTL_STATE_CONNECTING, ev, sizeof ev, &en) == CTL_OK)
            client_write(c, ev, en);
    }

    for (size_t off = 0; off < tries; ++off) {
/* ui closed the sock (timeout): stop routing so the next connect is clean */
        if (c && !client_still_open(c)) {
            ctl_action_t stop = { .kind = CTL_ACT_STOP };
            s->apply(s->apply_ctx, &stop);
            s->engine.state = CTL_STATE_IDLE;
            fprintf(stderr, "senkod: connect aborted (client gone)\n");
            return -1;
        }

        size_t i = (base + off) % st->n;
        const vl_server_t *sv = &st->servers[i];
        if (!server_supported(sv)) {
            connect_failure_set(&last_fail, "server", "unsupported protocol or security");
            continue;
        }

        ctl_cmd_t cmd;
        memset(&cmd, 0, sizeof cmd);
        cmd.kind = CTL_CMD_CONNECT;
        cmd.server_index = (int)i;

        char out[512]; size_t on = 0;
        ctl_action_t action;
        if (ctl_engine_handle(&s->engine, &cmd, out, sizeof out, &on, &action) != CTL_OK)
            continue;
        if (action.kind != CTL_ACT_START) continue;

        int r = s->apply(s->apply_ctx, &action);
        if (r != 0) {
            connect_failure_from_apply(&last_fail, r);
            connect_fail_notify_error(s, NULL, r);
            s->engine.state = CTL_STATE_IDLE;
            continue;
        }

        if (s->verify) {
            char vreason[120];
            vreason[0] = '\0';
            if (s->verify(s->apply_ctx, vreason, sizeof vreason) != 0) {
                connect_failure_set(&last_fail, "socks",
                                    vreason[0] ? vreason : "tunnel verify failed");
                ctl_action_t stop = { .kind = CTL_ACT_STOP };
                s->apply(s->apply_ctx, &stop);
                s->engine.state = CTL_STATE_IDLE;
                continue;
            }
        }

/* client left during verify: tear down the just-accepted tunnel */
        if (c && !client_still_open(c)) {
            ctl_action_t stop = { .kind = CTL_ACT_STOP };
            s->apply(s->apply_ctx, &stop);
            s->engine.state = CTL_STATE_IDLE;
            fprintf(stderr, "senkod: connect aborted after verify (client gone)\n");
            return -1;
        }

/* failover is transport state, not a user selection change */
        st->selected = requested_sel;
        if (s->persist && st->selected != orig_sel)
            s->persist(s->apply_ctx, st);
        if ((int)i != requested_sel)
            fprintf(stderr, "senkod: connected via fallback server %zu (requested %d)\n",
                    i, requested_sel);
        if (c) {
            if (on > 0) client_write(c, out, on);
            char ev[64]; size_t en = 0;
            if (ctl_engine_notify(&s->engine, CTL_STATE_CONNECTED, ev, sizeof ev, &en) == CTL_OK)
                client_write(c, ev, en);
        } else {
            char ev[64]; size_t en = 0;
            (void)ctl_engine_notify(&s->engine, CTL_STATE_CONNECTED, ev, sizeof ev, &en);
        }
        return 0;
    }

    fprintf(stderr, "senkod: connect failed after %zu tries: %s: %s\n",
            tries,
            last_fail.layer[0] ? last_fail.layer : "server",
            last_fail.reason[0] ? last_fail.reason : "unknown");
/* force stop so a half-applied route cannot block the next connect */
    {
        ctl_action_t stop = { .kind = CTL_ACT_STOP };
        s->apply(s->apply_ctx, &stop);
        s->engine.state = CTL_STATE_IDLE;
    }
/* keep the requested row selected so an error cannot move the ui */
    st->selected = requested_sel;
    if (s->persist) s->persist(s->apply_ctx, st);
    if (c && client_still_open(c)) {
        connect_failure_write(c, &last_fail);
        char ev[64]; size_t en = 0;
        if (ctl_engine_notify(&s->engine, CTL_STATE_ERROR, ev, sizeof ev, &en) == CTL_OK)
            client_write(c, ev, en);
    } else {
        char ev[64]; size_t en = 0;
        (void)ctl_engine_notify(&s->engine, CTL_STATE_ERROR, ev, sizeof ev, &en);
    }
    return -1;
}

static int apply_connect_action(ctl_server_t *s, ctl_client_t *c, ctl_action_t *action) {
    if (!s->apply) return -1;
    int r = s->apply(s->apply_ctx, action);
    if (r == 0 && action->kind == CTL_ACT_START) {
        if (s->verify) {
            char vreason[120];
            vreason[0] = '\0';
            if (s->verify(s->apply_ctx, vreason, sizeof vreason) != 0) {
                ctl_action_t stop = { .kind = CTL_ACT_STOP };
                s->apply(s->apply_ctx, &stop);
                s->engine.state = CTL_STATE_IDLE;
                if (c) {
                    connect_failure_t f;
                    connect_failure_set(&f, "socks",
                                        vreason[0] ? vreason : "tunnel verify failed");
                    connect_failure_write(c, &f);
                }
                return -1;
            }
        }
        char ev[64]; size_t en = 0;
        if (ctl_engine_notify(&s->engine, CTL_STATE_CONNECTED, ev, sizeof ev, &en) == CTL_OK) {
            if (c) client_write(c, ev, en);
        }
    } else if (r != 0 && action->kind == CTL_ACT_START) {
        if (c) {
            connect_failure_t f;
            connect_failure_from_apply(&f, r);
            connect_failure_write(c, &f);
            char ev[64]; size_t en = 0;
            if (ctl_engine_notify(&s->engine, CTL_STATE_ERROR, ev, sizeof ev, &en) == CTL_OK)
                client_write(c, ev, en);
        } else {
            char ev[64]; size_t en = 0;
            (void)ctl_engine_notify(&s->engine, CTL_STATE_ERROR, ev, sizeof ev, &en);
        }
    }
    return r;
}

int ctl_server_restore_tunnel(ctl_server_t *s) {
    if (!s || !s->apply) return -1;
    const store_t *st = &s->engine.store;
    if (st->selected < 0 || (size_t)st->selected >= st->n) return -1;
    if (s->engine.state == CTL_STATE_CONNECTED ||
        s->engine.state == CTL_STATE_CONNECTING)
        return 0;
    return connect_with_tunnel_pick(s, NULL, st->selected);
}

static int server_supported(const vl_server_t *sv) {
    return cfg_validate_server(sv, NULL, 0);
}

static void dispatch_line(ctl_server_t *s, ctl_client_t *c,
                           const char *line, size_t len) {
    ctl_cmd_t cmd;
    if (ctl_parse_cmd(line, len, &cmd) != CTL_OK) {
        char err[64]; size_t en = 0;
        if (ctl_build_err("bad command", err, sizeof err, &en) == CTL_OK)
            client_write(c, err, en);
        return;
    }

    if (cmd.kind == CTL_CMD_AUTH) {
        char reply[64]; size_t rn = 0;
        if (!s->require_auth || !s->token[0]) {
            c->authed = 1;
            if (ctl_build_ok("authed", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        if (strcmp(cmd.text, s->token) == 0) {
            c->authed = 1;
            if (ctl_build_ok("authed", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
        } else {
            if (ctl_build_err("auth failed", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            drop_client(c);
        }
        return;
    }

/* kick only needs liveness; requiring token here deadlocks boot */
    if (cmd.kind != CTL_CMD_STATUS && s->require_auth && !c->authed) {
        char err[64]; size_t en = 0;
        if (ctl_build_err("auth required", err, sizeof err, &en) == CTL_OK)
            client_write(c, err, en);
        return;
    }

/* engine reply buf is 512 bytes, list/fetch stream from here */
    if (cmd.kind == CTL_CMD_FETCH) {
        char reply[128]; size_t rn = 0;
        if (!s->fetch) {
            if (ctl_build_err("fetch not supported", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        unsigned char *blob = NULL;
        size_t blen = 0;
        if (fetch_body_alloc(s, cmd.text, &blob, &blen) != 0) {
            if (ctl_build_err("fetch failed", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        fetch_reply_body(c, blob, blen);
        free(blob);
        return;
    }

    if (cmd.kind == CTL_CMD_LIST) {
/* repair groups left by older subscription packing */
        store_normalize(&s->engine.store);
        const store_t *st = &s->engine.store;
        char ln[640]; size_t lnn = 0;
/* subs first so the ui can title groups before painting servers */
        for (size_t i = 0; i < STORE_MAX_SUBS; ++i) {
            if (!st->subs[i].used) continue;
            if (ctl_build_sub((int)i, st->subs[i].name, st->subs[i].url,
                              ln, sizeof ln, &lnn) == CTL_OK)
                client_write(c, ln, lnn);
        }
        for (size_t i = 0; i < st->n; ++i) {
            const vl_server_t *sv = &st->servers[i];
            int sel = (st->selected >= 0 && (size_t)st->selected == i) ? 1 : 0;
            if (ctl_build_srv((int)i, sel, st->group[i],
                              server_proto_name(sv), server_net_name(sv),
                              vl_sec_name(sv->security), server_supported(sv),
                              sv->host, sv->port, sv->remark,
                              ln, sizeof ln, &lnn) == CTL_OK)
                client_write(c, ln, lnn);
        }
        if (ctl_build_listend((int)st->n, ln, sizeof ln, &lnn) == CTL_OK)
            client_write(c, ln, lnn);
        return;
    }

    if (cmd.kind == CTL_CMD_CONNECT) {
        if (awg_tunnel_running()) {
            char err[64]; size_t en = 0;
            if (ctl_build_err("amneziawg active", err, sizeof err, &en) == CTL_OK)
                client_write(c, err, en);
            return;
        }
        if (cmd.server_index < 0 || (size_t)cmd.server_index >= s->engine.store.n) {
            char err[64]; size_t en = 0;
            if (ctl_build_err("no such server", err, sizeof err, &en) == CTL_OK)
                client_write(c, err, en);
            return;
        }
        (void)connect_with_tunnel_pick(s, c, cmd.server_index);
        return;
    }

    char out[512]; size_t on = 0;
    ctl_action_t action;
    ctl_engine_handle(&s->engine, &cmd, out, sizeof out, &on, &action);
    if (on > 0) client_write(c, out, on);

/* config writes must hit disk before senkod restarts */
    const char *ok_line = NULL;
    if (on >= 3 && memcmp(out, "OK ", 3) == 0)
        ok_line = out;
    else if (on > 4)
        ok_line = strstr(out, "\nOK ");
    if (s->persist && ok_line &&
        (cmd.kind == CTL_CMD_ADD_SERVER || cmd.kind == CTL_CMD_ADD_SUB ||
         cmd.kind == CTL_CMD_DEL_SERVER || cmd.kind == CTL_CMD_DEL_SUB) &&
        (ok_line == out || ok_line[0] == '\n')) {
        s->persist(s->apply_ctx, &s->engine.store);
    }

/* refresh needs the client fd because the engine has no socket */
    if (action.kind == CTL_ACT_REFRESH) {
        int si = action.server_index;
        char reply[128]; size_t rn = 0;
        if (!s->fetch) {
            if (ctl_build_err("refresh not supported", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        if (si < 0 || si >= STORE_MAX_SUBS || !s->engine.store.subs[si].used) {
            if (ctl_build_err("no such subscription", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        unsigned char *blob = NULL;
        size_t blen = 0;
        if (fetch_body_alloc(s, s->engine.store.subs[si].url, &blob, &blen) != 0) {
            if (ctl_build_err("fetch failed", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        size_t added = 0;
        if (store_refresh_sub(&s->engine.store, (size_t)si, (const char *)blob, blen, &added) != STORE_OK) {
            free(blob);
            if (ctl_build_err("refresh parse failed", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        free(blob);
        if (s->persist) s->persist(s->apply_ctx, &s->engine.store);
        char msg[96];
/* expose capacity overflow instead of silently dropping subscription nodes */
        if (s->engine.store.n >= STORE_MAX_SERVERS)
            snprintf(msg, sizeof msg, "refreshed %zu server(s) (list full)", added);
        else
            snprintf(msg, sizeof msg, "refreshed %zu server(s)", added);
        if (ctl_build_ok(msg, reply, sizeof reply, &rn) == CTL_OK)
            client_write(c, reply, rn);
        return;
    }

/* use the active path once the tunnel owns the data plane */
    if (action.kind == CTL_ACT_PING) {
        int ms = -1;
        if ((s->engine.state == CTL_STATE_CONNECTED ||
             s->engine.state == CTL_STATE_CONNECTING) && s->tunnel_probe)
            ms = s->tunnel_probe(s->apply_ctx);
        else if (s->probe)
            ms = s->probe(s->apply_ctx, action.server.host, action.server.port);
        char reply[64]; size_t rn = 0;
        if (ctl_build_pong(action.server_index, ms, reply, sizeof reply, &rn) == CTL_OK)
            client_write(c, reply, rn);
        return;
    }

    if (action.kind != CTL_ACT_NONE && s->apply)
        (void)apply_connect_action(s, c, &action);
}

static void service_client(ctl_server_t *s, ctl_client_t *c) {
    char buf[1024];
    ssize_t n = read(c->fd, buf, sizeof buf);
    if (n == 0) { drop_client(c); return; }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
        drop_client(c);
        return;
    }

    for (ssize_t i = 0; i < n; ++i) {
        char ch = buf[i];
        if (ch == '\n') {
            dispatch_line(s, c, c->inbuf, c->in_len);
            c->in_len = 0;
        } else if (c->in_len < sizeof c->inbuf) {
            c->inbuf[c->in_len++] = ch;
        } else {
            client_write(c, "ERR line too long\n", 18);
            c->in_len = 0;
        }
        if (c->fd < 0) return; /* dispatch may have dropped us */
    }
}

ctls_status_t ctl_server_step(ctl_server_t *s, int timeout_ms) {
    if (!s) return CTLS_ERR_ARG;

    struct pollfd pfd[1 + CTL_SERVER_MAX_CLIENTS];
    ctl_client_t *map[1 + CTL_SERVER_MAX_CLIENTS];
    nfds_t nf = 0;

    pfd[nf].fd = s->listen_fd;
    pfd[nf].events = POLLIN;
    map[nf] = NULL;
    nf++;

    for (size_t i = 0; i < CTL_SERVER_MAX_CLIENTS; ++i) {
        if (s->clients[i].fd < 0) continue;
        pfd[nf].fd = s->clients[i].fd;
        pfd[nf].events = POLLIN;
        if (s->clients[i].out_len > s->clients[i].out_off)
            pfd[nf].events |= POLLOUT;
        map[nf] = &s->clients[i];
        nf++;
    }

    int r = poll(pfd, nf, timeout_ms);
    if (r < 0) return (errno == EINTR) ? CTLS_OK : CTLS_ERR;
    if (r == 0) return CTLS_OK;

    if (pfd[0].revents & POLLIN) accept_one(s);

    for (nfds_t i = 1; i < nf; ++i) {
        ctl_client_t *c = map[i];
        if (!c || c->fd < 0) continue;
        if (pfd[i].revents & POLLOUT)
            (void)client_flush(c);
        if (c->fd < 0) continue;
        if (pfd[i].revents & (POLLIN | POLLHUP | POLLERR))
            service_client(s, c);
    }
    return CTLS_OK;
}

void ctl_server_broadcast(ctl_server_t *s, const char *line, size_t len) {
    if (!s || !line) return;
    for (size_t i = 0; i < CTL_SERVER_MAX_CLIENTS; ++i) {
        if (s->clients[i].fd < 0) continue;
        if (s->require_auth && !s->clients[i].authed) continue;
        client_write(&s->clients[i], line, len);
    }
}

size_t ctl_server_client_count(const ctl_server_t *s) {
    if (!s) return 0;
    size_t n = 0;
    for (size_t i = 0; i < CTL_SERVER_MAX_CLIENTS; ++i)
        if (s->clients[i].fd >= 0) n++;
    return n;
}

void ctl_server_close(ctl_server_t *s) {
    if (!s) return;
    for (size_t i = 0; i < CTL_SERVER_MAX_CLIENTS; ++i)
        if (s->clients[i].fd >= 0) drop_client(&s->clients[i]);
    if (s->listen_fd >= 0) { close(s->listen_fd); s->listen_fd = -1; }
    if (s->sock_path[0]) (void)remove_stale_socket(s->sock_path);
    if (s->token_path[0]) (void)unlink(s->token_path);
    s->token[0] = '\0';
    s->require_auth = 0;
}
