#define _DEFAULT_SOURCE

#include "ctl_server.h"
#include "core/b64.h"
#include "core/control.h"
#include "core/url.h"
#include "core/config.h"
#include "core/happ.h"
#include "core/store.h"
#include "daemon_ctl.h"
#include "../common/senko_paths.h"

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

/* colocating the token with the socket keeps both inside one protected runtime boundary */
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

#define CTL_MOBILE_GID 501 /* mobile group id */

static int write_ctl_token(const char *path, char *token_out, size_t token_cap) {
    if (!path || !token_out || token_cap < 33) return -1;
    unsigned char raw[16];
    int ur = open("/dev/urandom", O_RDONLY);
    ssize_t got = (ur >= 0) ? read(ur, raw, sizeof raw) : -1;
    if (ur >= 0) close(ur);
    if (got != (ssize_t)sizeof raw) return -1;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof raw; ++i) {
        token_out[i * 2]     = hex[raw[i] >> 4];
        token_out[i * 2 + 1] = hex[raw[i] & 0xf];
    }
    token_out[32] = '\0';

    char tmp[128];
    int tn = snprintf(tmp, sizeof tmp, "%s.%ld.tmp", path, (long)getpid());
    if (tn <= 0 || (size_t)tn >= sizeof tmp) return -1;
    (void)unlink(tmp);
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(tmp, flags, 0640);
    if (fd < 0) return -1;
    char line[48];
    int ln = snprintf(line, sizeof line, "%s\n", token_out);
    ssize_t w = (ln > 0) ? write(fd, line, (size_t)ln) : -1;
    int secure = fchmod(fd, 0640) == 0;
    if (fchown(fd, 0, CTL_MOBILE_GID) != 0 && geteuid() == 0) secure = 0;
    if (fsync(fd) != 0) secure = 0;
    if (close(fd) != 0) secure = 0;
    if (w != (ssize_t)ln) {
        unlink(tmp);
        return -1;
    }
    if (!secure || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static int tighten_sock_perms(const char *path) {
    if (!path || !path[0]) return -1;
    if (chown(path, 0, CTL_MOBILE_GID) != 0 && geteuid() == 0) return -1;
    return chmod(path, 0660);
}

static int token_equal(const char *a, const char *b) {
    size_t al = a ? strlen(a) : 0;
    size_t bl = b ? strlen(b) : 0;
    unsigned char diff = (unsigned char)(al ^ bl);
    size_t n = al > bl ? al : bl;
    for (size_t i = 0; i < n; ++i) {
        unsigned char ac = i < al ? (unsigned char)a[i] : 0;
        unsigned char bc = i < bl ? (unsigned char)b[i] : 0;
        diff |= (unsigned char)(ac ^ bc);
    }
    return diff == 0;
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
    if (tighten_sock_perms(path) != 0) {
        close(fd);
        unlink(path);
        return CTLS_ERR_BIND;
    }
    if (listen(fd, 4) != 0) {
        close(fd);
        unlink(path);
        return CTLS_ERR_BIND;
    }
    if (write_ctl_token(s->token_path, s->token, sizeof s->token) != 0) {
        close(fd);
        unlink(path);
        return CTLS_ERR_AUTH;
    }
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

void ctl_server_set_backup(ctl_server_t *s, ctl_backup_fn backup) {
    if (!s) return;
    s->backup = backup;
}

void ctl_server_set_check(ctl_server_t *s, ctl_check_fn check) {
    if (!s) return;
    s->check = check;
}

void ctl_server_set_reason(ctl_server_t *s, ctl_reason_fn reason) {
    if (!s) return;
    s->reason = reason;
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
    if (getpeereid(fd, &uid, &gid) != 0)
        return 1; /* old ios may not expose peer creds; the token guards writes */
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
    c->authed = 0;
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
                            const char *request_header,
                            unsigned char **body_out, size_t *len_out,
                            ctl_fetch_meta_t *meta) {
    if (!s || !s->fetch || !url || !body_out || !len_out) return -1;
    *body_out = NULL;
    *len_out = 0;
    unsigned char *body = (unsigned char *)malloc(FETCH_BODY_MAX);
    if (!body) return -1;
    size_t len = 0;
    if (s->fetch(s->apply_ctx, url, request_header, body, FETCH_BODY_MAX, &len, meta) != 0 ||
        len > FETCH_BODY_MAX) {
        free(body);
        return -1;
    }
    *body_out = body;
    *len_out = len;
    return 0;
}

/* the default request pretends to be happ so panels that only answer known
   clients answer at all. those same panels then hand happ its own encrypted
   bundle, and the formats senko cannot open leave the user with nothing. one
   retry under senko's own name asks the panel for the plain feed instead */
#define SENKO_PLAIN_UA "User-Agent: Senko/2"

static int fetch_body_alloc(ctl_server_t *s, const char *url,
                            const char *request_header,
                            unsigned char **body_out, size_t *len_out,
                            ctl_fetch_meta_t *meta);

/* a body senko cannot unwrap, which is the only case worth a second request */
static int unreadable_happ_body(const unsigned char *blob, size_t blen) {
    char probe[8192];
    char plain[16384];
    size_t n;
    if (!blob || blen < 7) return 0;
    if (cfg_content_kind((const char *)blob, blen) != CFG_CONTENT_HAPP) return 0;
    n = blen < sizeof probe - 1 ? blen : sizeof probe - 1;
    memcpy(probe, blob, n);
    probe[n] = '\0';
    return happ_unwrap(probe, plain, sizeof plain) != 0;
}

#define LOG_TAIL_MAX (48 * 1024)

#define IMPORT_STAGE_MAX (512 * 1024)

/* the panel host is the only name a bare subscription url carries */
static void sub_name_from_url(const char *url, char *out, size_t cap) {
    const char *host = url;
    size_t n = 0;
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!url) return;
    if (strncmp(host, "https://", 8) == 0) host += 8;
    else if (strncmp(host, "http://", 7) == 0) host += 7;
    while (host[n] && host[n] != '/' && host[n] != ':' && host[n] != '?' &&
           n + 1 < cap)
        ++n;
    if (n == 0) {
        snprintf(out, cap, "subscription");
        return;
    }
    memcpy(out, host, n);
    out[n] = '\0';
}

/* register the feed the deep link pointed at, then pull it once so the user
   sees nodes instead of an empty section */
static void import_subscription_url(ctl_server_t *s, ctl_client_t *c,
                                    const char *url) {
    char reply[288];
    size_t rn = 0;
    char name[128];
    size_t si = 0;
    unsigned char *blob = NULL;
    size_t blen = 0;
    size_t added = 0;
    ctl_fetch_meta_t meta;

    sub_name_from_url(url, name, sizeof name);
    if (store_add_sub(&s->engine.store, name, url, &si) != STORE_OK) {
        if (ctl_build_err("subscription list is full", reply, sizeof reply, &rn) == CTL_OK)
            client_write(c, reply, rn);
        return;
    }
    if (s->persist) s->persist(s->apply_ctx, &s->engine.store);

    if (!s->fetch) {
        if (ctl_build_ok("subscription added", reply, sizeof reply, &rn) == CTL_OK)
            client_write(c, reply, rn);
        return;
    }
    memset(&meta, 0, sizeof meta);
    if (fetch_body_alloc(s, url, NULL, &blob, &blen, &meta) != 0) {
        if (ctl_build_ok("subscription added, refresh failed", reply, sizeof reply, &rn) == CTL_OK)
            client_write(c, reply, rn);
        return;
    }
    if (unreadable_happ_body(blob, blen)) {
        free(blob);
        blob = NULL;
        memset(&meta, 0, sizeof meta);
        if (fetch_body_alloc(s, url, SENKO_PLAIN_UA, &blob, &blen, &meta) != 0)
            blob = NULL;
    }
    if (!blob || meta.gated ||
        store_refresh_sub(&s->engine.store, si, (const char *)blob, blen,
                          &added) != STORE_OK) {
        const char *why = blob ? cfg_reject_reason((const char *)blob, blen, url) : NULL;
        char msg[224];
        snprintf(msg, sizeof msg, "subscription added, %s",
                 why ? why : "refresh failed");
        free(blob);
        if (ctl_build_ok(msg, reply, sizeof reply, &rn) == CTL_OK)
            client_write(c, reply, rn);
        return;
    }
    if (meta.expire) store_set_sub_expire(&s->engine.store, si, meta.expire);
    store_set_sub_meta(&s->engine.store, si, meta.upload, meta.download,
                       meta.total, meta.description, meta.support_url);
    free(blob);
    if (s->persist) s->persist(s->apply_ctx, &s->engine.store);
    snprintf(name, sizeof name, "subscription added, %zu server(s)", added);
    if (ctl_build_ok(name, reply, sizeof reply, &rn) == CTL_OK)
        client_write(c, reply, rn);
}

/* read a staged import file whole; the cap is what the parser is allowed to see */
static int read_whole_file(const char *path, size_t max_bytes,
                           unsigned char **out, size_t *out_len) {
    if (!path || !out || !out_len) return -1;
    *out = NULL;
    *out_len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char *buf = (unsigned char *)malloc(max_bytes);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, max_bytes, f);
    int overflowed = (got == max_bytes && fgetc(f) != EOF);
    fclose(f);
    if (overflowed) { free(buf); return -1; }
    *out = buf;
    *out_len = got;
    return 0;
}

/* read the last bytes of a log file, starting at a line boundary */
static int read_log_tail(const char *path, size_t max_bytes,
                         unsigned char **out, size_t *out_len) {
    if (!path || !out || !out_len || max_bytes == 0) return -1;
    *out = NULL;
    *out_len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return -1; }
    size_t want = (size_t)size > max_bytes ? max_bytes : (size_t)size;
    if (fseek(f, size - (long)want, SEEK_SET) != 0) { fclose(f); return -1; }
    unsigned char *buf = (unsigned char *)malloc(want ? want : 1);
    if (!buf) { fclose(f); return -1; }
    size_t got = want ? fread(buf, 1, want, f) : 0;
    fclose(f);
    size_t start = 0;
/* a truncated first line would reach the ui as a fragment */
    if ((size_t)size > want) {
        while (start < got && buf[start] != '\n') ++start;
        if (start < got) ++start;
    }
    memmove(buf, buf + start, got - start);
    *out = buf;
    *out_len = got - start;
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

static const char *connect_verify_layer(const char *reason) {
    return reason && strncmp(reason, "routing ", 8) == 0 ? "routing" : "socks";
}

static void connect_failure_from_apply(ctl_server_t *s, connect_failure_t *f, int r) {
    const char *detail = (s && s->reason) ? s->reason(s->apply_ctx) : NULL;
    if ((r == DCTL_ERR_ROUTING || r == DCTL_ERR_GO) && detail && detail[0]) {
        connect_failure_set(f, r == DCTL_ERR_GO ? "tunnel" : "routing", detail);
        return;
    }
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
    else if (r == DCTL_ERR_GO)
        connect_failure_set(f, "tunnel", "the go backend core could not start; open System Logs for the exact cause");
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
    connect_failure_from_apply(s, &f, r);
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

static int connect_with_tunnel_pick(ctl_server_t *s, ctl_client_t *c, int start_idx) {
    if (!s || !s->apply) return -1;
    store_t *st = &s->engine.store;
    if (!st->n) return -1;

    size_t base = (start_idx >= 0 && (size_t)start_idx < st->n) ? (size_t)start_idx : 0;
    size_t tries = 1;
    int orig_sel = st->selected;
    int requested_sel = (int)base;
    connect_failure_t last_fail;
    connect_failure_set(&last_fail, "server", "no working server");

    if (c) {
        char ev[64]; size_t en = 0;
        s->engine.state = CTL_STATE_CONNECTING;
        s->engine.connected_at = 0;
        if (ctl_build_state(CTL_STATE_CONNECTING, 0, ev, sizeof ev, &en) == CTL_OK)
            client_write(c, ev, en);
    }

    for (size_t off = 0; off < tries; ++off) {
/* a timed-out client cannot own routing state it can no longer observe */
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
            connect_failure_from_apply(s, &last_fail, r);
            connect_fail_notify_error(s, NULL, r);
            s->engine.state = CTL_STATE_IDLE;
            continue;
        }

        if (s->verify) {
            char vreason[120];
            vreason[0] = '\0';
            if (s->verify(s->apply_ctx, vreason, sizeof vreason) != 0) {
                connect_failure_set(&last_fail, connect_verify_layer(vreason),
                                    vreason[0] ? vreason : "tunnel verify failed");
                ctl_action_t stop = { .kind = CTL_ACT_STOP };
                s->apply(s->apply_ctx, &stop);
                s->engine.state = CTL_STATE_IDLE;
                continue;
            }
        }

/* client exit must not leave an unowned tunnel active */
        if (c && !client_still_open(c)) {
            ctl_action_t stop = { .kind = CTL_ACT_STOP };
            s->apply(s->apply_ctx, &stop);
            s->engine.state = CTL_STATE_IDLE;
            fprintf(stderr, "senkod: connect aborted after verify (client gone)\n");
            return -1;
        }

        st->selected = requested_sel;
        if (s->persist && st->selected != orig_sel)
            s->persist(s->apply_ctx, st);
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
    {
        ctl_action_t stop = { .kind = CTL_ACT_STOP };
        s->apply(s->apply_ctx, &stop);
        s->engine.state = CTL_STATE_IDLE;
    }
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
                    connect_failure_set(&f, connect_verify_layer(vreason),
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
            connect_failure_from_apply(s, &f, r);
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
        if (s->token[0] && token_equal(cmd.text, s->token)) {
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

    if (!c->authed) {
        char err[64]; size_t en = 0;
        if (ctl_build_err("auth required", err, sizeof err, &en) == CTL_OK)
            client_write(c, err, en);
        return;
    }

    if (cmd.kind == CTL_CMD_EXPORT || cmd.kind == CTL_CMD_RESTORE) {
        char reply[128]; size_t rn = 0;
        int restore = cmd.kind == CTL_CMD_RESTORE;
        int ok = s->backup && s->backup(s->apply_ctx, restore,
                                        &s->engine.store) == 0;
        if (ok) {
            if (ctl_build_ok(restore ? "backup restored" : "backup exported",
                             reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
        } else if (ctl_build_err(restore ? "backup is invalid" : "backup export failed",
                                 reply, sizeof reply, &rn) == CTL_OK) {
            client_write(c, reply, rn);
        }
        return;
    }

    if (cmd.kind == CTL_CMD_CHECK) {
        char reply[160]; size_t rn = 0;
        if (!s->check || cmd.server_index < 0 ||
            (size_t)cmd.server_index >= s->engine.store.n) {
            if (ctl_build_err("check unavailable", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        char reason[96];
        reason[0] = '\0';
        int ms = s->check(s->apply_ctx, cmd.name,
                          &s->engine.store.servers[cmd.server_index],
                          reason, sizeof reason);
        if (ms >= 0) {
            if (ctl_build_pong(cmd.server_index, ms, reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
        } else if (ctl_build_err(reason[0] ? reason : "check failed",
                                 reply, sizeof reply, &rn) == CTL_OK) {
            client_write(c, reply, rn);
        }
        return;
    }

    if (cmd.kind == CTL_CMD_CLEAR_MANUAL) {
        char reply[96]; size_t rn = 0;
        if (s->engine.state == CTL_STATE_CONNECTED ||
            s->engine.state == CTL_STATE_CONNECTING) {
            if (ctl_build_err("disconnect first", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        size_t removed = 0;
        if (store_clear_manual(&s->engine.store, &removed) != STORE_OK) {
            if (ctl_build_err("could not clear manual servers",
                              reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        if (s->persist) s->persist(s->apply_ctx, &s->engine.store);
        char msg[64];
        snprintf(msg, sizeof msg, "removed %zu server(s)", removed);
        if (ctl_build_ok(msg, reply, sizeof reply, &rn) == CTL_OK)
            client_write(c, reply, rn);
        return;
    }

    if (cmd.kind == CTL_CMD_IMPORT) {
        char reply[128]; size_t rn = 0;
        unsigned char *blob = NULL;
        size_t blen = 0;
        if (read_whole_file(SENKO_IMPORT_STAGE, IMPORT_STAGE_MAX, &blob, &blen) != 0 ||
            blen == 0) {
            free(blob);
            (void)unlink(SENKO_IMPORT_STAGE);
            if (ctl_build_err("nothing to import", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        (void)unlink(SENKO_IMPORT_STAGE);

        cfg_content_t kind = cfg_content_kind((const char *)blob, blen);
        if (kind == CFG_CONTENT_UNKNOWN) {
            free(blob);
            if (ctl_build_err("unknown content type", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }

/* a happ deep link normally carries the panel's subscription url, which is not
   a node to add but a feed to follow */
        char sub_url[512];
        if (cfg_subscription_url((const char *)blob, blen, sub_url, sizeof sub_url) == 0) {
            free(blob);
            import_subscription_url(s, c, sub_url);
            return;
        }

        vl_server_t *parsed = (vl_server_t *)calloc(STORE_MAX_SERVERS, sizeof *parsed);
        if (!parsed) {
            free(blob);
            if (ctl_build_err("out of memory", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        size_t found = 0;
        (void)cfg_parse_subscription((const char *)blob, blen, parsed,
                                     STORE_MAX_SERVERS, &found);
        free(blob);
        if (found == 0) {
            free(parsed);
            if (ctl_build_err("no server senko can run in this file",
                              reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        size_t added = 0, skipped = 0;
        for (size_t i = 0; i < found; ++i) {
            if (store_add_manual_server(&s->engine.store, &parsed[i], NULL) == STORE_OK)
                added++;
            else
                skipped++;
        }
        free(parsed);
        if (added && s->persist) s->persist(s->apply_ctx, &s->engine.store);
        char msg[96];
        if (skipped)
            snprintf(msg, sizeof msg, "imported %zu server(s), skipped %zu",
                     added, skipped);
        else
            snprintf(msg, sizeof msg, "imported %zu server(s)", added);
        if (added == 0) {
            if (ctl_build_err("every server in this file is already saved",
                              reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        if (ctl_build_ok(msg, reply, sizeof reply, &rn) == CTL_OK)
            client_write(c, reply, rn);
        return;
    }

    if (cmd.kind == CTL_CMD_HWID) {
        char hwid[65];
        char reply[128]; size_t rn = 0;
        url_device_hwid(hwid, sizeof hwid);
        if (!hwid[0]) {
            if (ctl_build_err("no device id", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        if (ctl_build_ok(hwid, reply, sizeof reply, &rn) == CTL_OK)
            client_write(c, reply, rn);
        return;
    }

    if (cmd.kind == CTL_CMD_LOGS) {
/* the ui runs as mobile and, on jailbreaks that keep the app sandboxed, cannot
   open the log at all, so the daemon that owns the file hands it over */
        char reply[128]; size_t rn = 0;
        unsigned char *tail = NULL;
        size_t tlen = 0;
        if (read_log_tail(SENKO_SYSTEM_LOG, LOG_TAIL_MAX, &tail, &tlen) != 0) {
            if (ctl_build_err("no daemon log", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        fetch_reply_body(c, tail, tlen);
        free(tail);
        return;
    }

    if (cmd.kind == CTL_CMD_FETCH) {
        char reply[128]; size_t rn = 0;
        if (!s->fetch) {
            if (ctl_build_err("fetch not supported", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        unsigned char *blob = NULL;
        size_t blen = 0;
        if (fetch_body_alloc(s, cmd.text, NULL, &blob, &blen, NULL) != 0) {
            if (ctl_build_err("fetch failed", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
        fetch_reply_body(c, blob, blen);
        free(blob);
        return;
    }

    if (cmd.kind == CTL_CMD_LIST) {
        store_normalize(&s->engine.store);
        const store_t *st = &s->engine.store;
        char ln[2048]; size_t lnn = 0;
        for (size_t i = 0; i < store_section_count(st); ++i) {
            int section = store_section_at(st, i);
            if (section < 0 || section >= STORE_MAX_SUBS || !st->subs[section].used)
                continue;
            if (ctl_build_sub(section, st->subs[section].name, st->subs[section].url,
                              ln, sizeof ln, &lnn) == CTL_OK)
                client_write(c, ln, lnn);
            if (ctl_build_submeta(section, st->subs[section].expire,
                                  ln, sizeof ln, &lnn) == CTL_OK)
                client_write(c, ln, lnn);
            if (ctl_build_subinfo(section, st->subs[section].upload,
                                  st->subs[section].download,
                                  st->subs[section].total,
                                  st->subs[section].description,
                                  st->subs[section].support_url,
                                  ln, sizeof ln, &lnn) == CTL_OK)
                client_write(c, ln, lnn);
            if (ctl_build_subhdr(section, st->subs[section].header,
                                 ln, sizeof ln, &lnn) == CTL_OK)
                client_write(c, ln, lnn);
        }
        if (store_section_count(st) > 0) {
            int order[STORE_MAX_SUBS + 1];
            size_t order_n = store_section_count(st);
            if (order_n > STORE_MAX_SUBS + 1) order_n = STORE_MAX_SUBS + 1;
            for (size_t i = 0; i < order_n; ++i) order[i] = store_section_at(st, i);
            int order_len = snprintf(ln, sizeof ln, "SECTION");
            for (size_t i = 0; i < order_n && order_len > 0 &&
                               (size_t)order_len < sizeof ln; ++i) {
                int n = snprintf(ln + order_len, sizeof ln - (size_t)order_len,
                                 " %d", order[i]);
                if (n < 0 || (size_t)n >= sizeof ln - (size_t)order_len) {
                    order_len = -1;
                    break;
                }
                order_len += n;
            }
            if (order_len > 0 && (size_t)order_len + 1 < sizeof ln) {
                ln[order_len++] = '\n';
                client_write(c, ln, (size_t)order_len);
            }
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

    const char *ok_line = NULL;
    if (on >= 3 && memcmp(out, "OK ", 3) == 0)
        ok_line = out;
    else if (on > 4)
        ok_line = strstr(out, "\nOK ");
    if (s->persist && ok_line &&
        (cmd.kind == CTL_CMD_ADD_SERVER || cmd.kind == CTL_CMD_ADD_SUB ||
         cmd.kind == CTL_CMD_SET_SUB_HEADER ||
         cmd.kind == CTL_CMD_DEL_SERVER || cmd.kind == CTL_CMD_DEL_SUB) &&
        (ok_line == out || ok_line[0] == '\n')) {
        s->persist(s->apply_ctx, &s->engine.store);
    }
    if (s->persist && ok_line &&
        (cmd.kind == CTL_CMD_MOVE_SECTION || cmd.kind == CTL_CMD_MOVE_MANUAL) &&
        (ok_line == out || ok_line[0] == '\n')) {
        s->persist(s->apply_ctx, &s->engine.store);
    }

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
        ctl_fetch_meta_t meta;
        memset(&meta, 0, sizeof meta);
        if (fetch_body_alloc(s, s->engine.store.subs[si].url,
                             s->engine.store.subs[si].header,
                             &blob, &blen, &meta) != 0) {
            if (ctl_build_err("fetch failed", reply, sizeof reply, &rn) == CTL_OK)
                client_write(c, reply, rn);
            return;
        }
/* a panel that answered the happ user agent with a bundle senko cannot open is
   asked once more under senko's own name, which is what makes it serve the
   plain feed. a subscription that carries its own header is left alone: the
   user chose that one */
        if (unreadable_happ_body(blob, blen) &&
            !s->engine.store.subs[si].header[0]) {
            unsigned char *plain_blob = NULL;
            size_t plain_len = 0;
            ctl_fetch_meta_t plain_meta;
            memset(&plain_meta, 0, sizeof plain_meta);
            if (fetch_body_alloc(s, s->engine.store.subs[si].url, SENKO_PLAIN_UA,
                                 &plain_blob, &plain_len, &plain_meta) == 0) {
                free(blob);
                blob = plain_blob;
                blen = plain_len;
                meta = plain_meta;
            }
        }
        if (meta.gated) {
/* the panel answers a device it will not serve with a one entry placeholder
   profile, so keeping the previous nodes is the only correct outcome */
            free(blob);
            char msg[320];
            snprintf(msg, sizeof msg, "subscription refused this device: %s",
                     meta.gate_reason);
            char gerr[352]; size_t gn = 0;
            if (ctl_build_err(msg, gerr, sizeof gerr, &gn) == CTL_OK)
                client_write(c, gerr, gn);
            return;
        }
        size_t added = 0;
        if (store_refresh_sub(&s->engine.store, (size_t)si, (const char *)blob, blen, &added) != STORE_OK) {
/* "parse failed" tells the user nothing they can act on, and the two answers a
   panel actually gives instead of a feed are both nameable */
            const char *why = cfg_reject_reason((const char *)blob, blen,
                                                s->engine.store.subs[si].url);
            char detail[224];
            size_t dn = 0;
            free(blob);
            if (ctl_build_err(why ? why : "refresh parse failed",
                              detail, sizeof detail, &dn) == CTL_OK)
                client_write(c, detail, dn);
            return;
        }
        free(blob);
        if (meta.expire)
            store_set_sub_expire(&s->engine.store, (size_t)si, meta.expire);
        store_set_sub_meta(&s->engine.store, (size_t)si, meta.upload,
                           meta.download, meta.total, meta.description,
                           meta.support_url);
        if (s->persist) s->persist(s->apply_ctx, &s->engine.store);
        char msg[96];
        if (s->engine.store.n >= STORE_MAX_SERVERS)
            snprintf(msg, sizeof msg, "refreshed %zu server(s) (list full)", added);
        else
            snprintf(msg, sizeof msg, "refreshed %zu server(s)", added);
        if (ctl_build_ok(msg, reply, sizeof reply, &rn) == CTL_OK)
            client_write(c, reply, rn);
        return;
    }

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
        if (!s->clients[i].authed) continue;
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
}
