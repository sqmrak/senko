#define _DEFAULT_SOURCE
#include "loop.h"
#include "pf_natlook.h"
#include "senko_trace.h"
#include "socks5.h"
#include <stdio.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define LOOP_OPEN_STACK_SZ (512 * 1024)

static void set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static void wake_loop(loop_t *lp) {
    if (!lp || lp->wake_wr < 0) return;
    char b = 'x';
    ssize_t n = write(lp->wake_wr, &b, 1);
    (void)n; /* a full pipe is already awake */
}

static void drain_wake(loop_t *lp) {
    if (!lp || lp->wake_rd < 0) return;
    char buf[64];
    for (;;) {
        ssize_t n = read(lp->wake_rd, buf, sizeof buf);
        if (n > 0) continue;
        if (n < 0 && errno == EINTR) continue;
        break;
    }
}

static void *open_worker_main(void *arg) {
    loop_conn_t *c = (loop_conn_t *)arg;
    loop_t *lp = c->owner;
    void *th = NULL;

/* keep retries short */
    for (int attempt = 0; attempt < 3; ++attempt) {
        pthread_mutex_lock(&lp->open_lock);
        int cancelled = c->open_cancelled;
        pthread_mutex_unlock(&lp->open_lock);
        if (cancelled) break;
        if (attempt > 0) {
            usleep((useconds_t)(30000 * attempt));
            pthread_mutex_lock(&lp->open_lock);
            cancelled = c->open_cancelled;
            pthread_mutex_unlock(&lp->open_lock);
            if (cancelled) break;
        }
        if (c->remote_fd < 0) {
            int rfd = lp->dial(lp->dial_ctx);
            if (rfd < 0) continue;
            set_nonblock(rfd);
            c->remote_fd = rfd;
        }
        th = c->open_vt->open(c->remote_fd, &c->open_tls_cfg);
        if (th) break;
        close(c->remote_fd);
        c->remote_fd = -1;
    }

    pthread_mutex_lock(&lp->open_lock);
    c->open_th = th;
    c->open_done = 1;
    pthread_mutex_unlock(&lp->open_lock);

    wake_loop(lp);
    return NULL;
}

loop_status_t loop_init(loop_t *lp, uint16_t listen_port, int bind_public,
                        const transport_vt_t *vt,
                        loop_dialer_fn dial, void *dial_ctx,
                        vl_proto_t proto,
                        const uint8_t uuid[VLESS_UUID_LEN], const char *flow,
                        const char *user, const char *pass) {
    if (!lp || !vt || !dial) return LOOP_ERR_ARG;
    if (proto == VL_PROTO_VLESS && !uuid) return LOOP_ERR_ARG;
    memset(lp, 0, sizeof *lp);
    lp->listen_fd = -1;
    lp->tproxy_fd = -1;
    lp->wake_rd = -1;
    lp->wake_wr = -1;
    lp->vt = vt;
    lp->dial = dial;
    lp->dial_ctx = dial_ctx;
    lp->proto = proto;
    if (uuid) memcpy(lp->uuid, uuid, VLESS_UUID_LEN);
    if (flow && flow[0]) {
        size_t fl = strlen(flow);
        if (fl >= sizeof lp->flow) fl = sizeof lp->flow - 1;
        memcpy(lp->flow, flow, fl);
        lp->flow[fl] = '\0';
    }
    if (user) {
        size_t ul = strlen(user);
        if (ul >= sizeof lp->user) ul = sizeof lp->user - 1;
        memcpy(lp->user, user, ul);
        lp->user[ul] = '\0';
    }
    if (pass) {
        size_t pl = strlen(pass);
        if (pl >= sizeof lp->pass) pl = sizeof lp->pass - 1;
        memcpy(lp->pass, pass, pl);
        lp->pass[pl] = '\0';
    }

    /* point transport fields at buffers owned by the loop */
    lp->tls_cfg.sni         = lp->sni;
    lp->tls_cfg.fingerprint = lp->fingerprint;
    lp->tls_cfg.reality_pbk = lp->reality_pbk;
    lp->tls_cfg.reality_sid = lp->reality_sid;
    lp->tls_cfg.path        = lp->path;
    lp->tls_cfg.ws_host     = lp->ws_host;
    lp->tls_cfg.xhttp_mode  = lp->xhttp_mode;

    /* command-line mode starts active; managed mode selects a server later */
    lp->active = 1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return LOOP_ERR_BIND;

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    uint16_t ports[22];
    size_t nports = 0;
    if (listen_port == 0) {
        ports[nports++] = 0;
    } else {
        for (int off = 0; off < 20 && nports < 22; ++off)
            ports[nports++] = (uint16_t)(listen_port + off);
        ports[nports++] = 0;
    }

    uint32_t bind_addr = bind_public ? INADDR_ANY : htonl(INADDR_LOOPBACK);
    int bound = 0;
    for (size_t pi = 0; pi < nports; ++pi) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof addr);
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = bind_addr;
        addr.sin_port = htons(ports[pi]);
        if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0)
            continue;
        if (listen(fd, 32) != 0) {
            close(fd);
            fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) return LOOP_ERR_BIND;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
            continue;
        }
        bound = 1;
        break;
    }
    if (!bound) {
        close(fd);
        return LOOP_ERR_BIND;
    }
    set_nonblock(fd);
    lp->listen_fd = fd;

    int pfd[2];
    if (pipe(pfd) != 0) {
        close(fd);
        lp->listen_fd = -1;
        return LOOP_ERR_BIND;
    }
    set_nonblock(pfd[0]);
    set_nonblock(pfd[1]);
    lp->wake_rd = pfd[0];
    lp->wake_wr = pfd[1];
    if (pthread_mutex_init(&lp->open_lock, NULL) != 0) {
        close(lp->wake_rd);
        close(lp->wake_wr);
        close(fd);
        lp->listen_fd = lp->wake_rd = lp->wake_wr = -1;
        return LOOP_ERR_BIND;
    }
    lp->open_lock_ready = 1;
    return LOOP_OK;
}

uint16_t loop_listen_port(const loop_t *lp) {
    if (!lp) return 0;
    struct sockaddr_in addr;
    socklen_t len = sizeof addr;
    if (getsockname(lp->listen_fd, (struct sockaddr *)&addr, &len) != 0) return 0;
    return ntohs(addr.sin_port);
}

static void copy_field(char *dst, size_t cap, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    size_t l = strlen(src);
    if (l >= cap) l = cap - 1;
    memcpy(dst, src, l);
    dst[l] = '\0';
}

void loop_set_tls(loop_t *lp, const char *sni, const char *fingerprint,
                  const char *reality_pbk, const char *reality_sid,
                  const char *path, const char *ws_host,
                  const char *xhttp_mode) {
    if (!lp) return;
    copy_field(lp->sni,          sizeof lp->sni,          sni);
    copy_field(lp->fingerprint,  sizeof lp->fingerprint,  fingerprint);
    copy_field(lp->reality_pbk,  sizeof lp->reality_pbk,  reality_pbk);
    copy_field(lp->reality_sid,  sizeof lp->reality_sid,  reality_sid);
    copy_field(lp->path,         sizeof lp->path,         path);
    copy_field(lp->ws_host,      sizeof lp->ws_host,      ws_host);
    copy_field(lp->xhttp_mode,   sizeof lp->xhttp_mode,   xhttp_mode);
    lp->tls_cfg.sni = lp->sni;
    lp->tls_cfg.fingerprint = lp->fingerprint;
    lp->tls_cfg.reality_pbk = lp->reality_pbk;
    lp->tls_cfg.reality_sid = lp->reality_sid;
    lp->tls_cfg.path = lp->path;
    lp->tls_cfg.ws_host = lp->ws_host;
    lp->tls_cfg.xhttp_mode = lp->xhttp_mode;
}

static loop_conn_t *alloc_conn(loop_t *lp) {
    for (size_t i = 0; i < LOOP_MAX_CONNS; ++i) {
        if (!lp->conns[i].used) {
            loop_conn_t *c = &lp->conns[i];
            memset(c, 0, sizeof *c);
            c->owner = lp;
            c->local_fd = -1;
            c->remote_fd = -1;
            return c;
        }
    }
    return NULL;
}

static int flush_pend_local(loop_conn_t *c);
static int flush_to_local(loop_conn_t *c);
static void cancel_opening_conn(loop_t *lp, loop_conn_t *c);

static void clear_conn_slot(loop_conn_t *c) {
    loop_t *owner = c->owner;
    memset(c, 0, sizeof *c);
    c->owner = owner;
    c->local_fd = c->remote_fd = -1;
}

static int pend_append(loop_conn_t *c, const uint8_t *buf, size_t len) {
    if (!c || !buf || len == 0) return 0;
    if (c->pend_len + len > sizeof c->pend) return -1;
    memcpy(c->pend + c->pend_len, buf, len);
    c->pend_len += len;
    return 0;
}

static int read_local_into_prebuf(loop_conn_t *c) {
    uint8_t buf[256];
    for (;;) {
        size_t room = sizeof c->prebuf - c->prebuf_len;
        if (room == 0) return 0;
        size_t want = room < sizeof buf ? room : sizeof buf;
        ssize_t n = read(c->local_fd, buf, want);
        if (n > 0) {
            memcpy(c->prebuf + c->prebuf_len, buf, (size_t)n);
            c->prebuf_len += (size_t)n;
            continue;
        }
        if (n == 0) return -1;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        if (errno == EINTR) continue;
        return -1;
    }
}

/* answer SOCKS while the worker opens the remote transport */
static void service_opening_local(loop_t *lp, loop_conn_t *c, short local_re) {
    if (local_re & (POLLHUP | POLLERR)) {
        cancel_opening_conn(lp, c);
        return;
    }
    if (local_re & POLLOUT) {
        if (flush_pend_local(c) != 0)
            cancel_opening_conn(lp, c);
    }
    if (local_re & POLLIN) {
        if (read_local_into_prebuf(c) != 0) {
            cancel_opening_conn(lp, c);
            return;
        }
    }

    if (!c->transparent && !c->socks_greet_done && c->prebuf_len > 0) {
        size_t used = 0;
        s5_status_t gr = socks5_parse_greeting(c->prebuf, c->prebuf_len, &used);
        if (gr == S5_OK) {
            uint8_t reply[2];
            size_t rn = 0;
            if (socks5_build_method_reply(reply, sizeof reply, &rn) != S5_OK ||
                pend_append(c, reply, rn) != 0 ||
                flush_pend_local(c) != 0) {
                cancel_opening_conn(lp, c);
                return;
            }
            c->socks_greet_done = 1;
            memmove(c->prebuf, c->prebuf + used, c->prebuf_len - used);
            c->prebuf_len -= used;
        } else if (gr != S5_NEED_MORE) {
            cancel_opening_conn(lp, c);
            return;
        }
    }

}

static void cancel_opening_conn(loop_t *lp, loop_conn_t *c) {
    if (!c->used || !c->opening) return;
    pthread_mutex_lock(&lp->open_lock);
    c->open_cancelled = 1;
    pthread_mutex_unlock(&lp->open_lock);
    if (c->local_fd >= 0) {
        close(c->local_fd);
        c->local_fd = -1;
    }
    /* wake the worker without reusing its remote fd */
    if (c->remote_fd >= 0)
        shutdown(c->remote_fd, SHUT_RDWR);
}

static void drop_conn(loop_t *lp, loop_conn_t *c) {
    if (!c->used) return;
    if (c->opening) {
        cancel_opening_conn(lp, c);
        return;
    }
    session_trace_close(&c->sess, "drop");
    if (c->sess.state == SESS_ERROR)
        fprintf(stderr, "senkod: dropping errored session\n");
    if (c->th && c->open_vt) c->open_vt->close(c->th);
    if (c->remote_fd >= 0) close(c->remote_fd);
    if (c->local_fd >= 0)  close(c->local_fd);
    clear_conn_slot(c);
    if (lp->nconns > 0) lp->nconns--;
}

/* drop connections when switching servers; keep the listener open */
static void drop_all_conns(loop_t *lp) {
    for (size_t i = 0; i < LOOP_MAX_CONNS; ++i) {
        if (!lp->conns[i].used) continue;
        if (lp->conns[i].opening) cancel_opening_conn(lp, &lp->conns[i]);
        else drop_conn(lp, &lp->conns[i]);
    }
    for (size_t i = 0; i < LOOP_MAX_CONNS; ++i) {
        loop_conn_t *c = &lp->conns[i];
        if (!c->used || !c->opening) continue;
        pthread_join(c->open_thread, NULL);
        c->opening = 0;
        c->open_done = 0;
        c->open_th = NULL;
        if (lp->nopening > 0) lp->nopening--;
        if (c->remote_fd >= 0) close(c->remote_fd);
        if (c->local_fd >= 0) close(c->local_fd);
        clear_conn_slot(c);
    }
}

static void reap_opening_conns(loop_t *lp) {
    for (size_t i = 0; i < LOOP_MAX_CONNS; ++i) {
        loop_conn_t *c = &lp->conns[i];
        if (!c->used || !c->opening) continue;

        pthread_mutex_lock(&lp->open_lock);
        int done = c->open_done;
        int cancelled = c->open_cancelled;
        void *th = c->open_th;
        pthread_mutex_unlock(&lp->open_lock);
        if (!done) continue;

        pthread_join(c->open_thread, NULL);
        c->opening = 0;
        c->open_done = 0;
        c->open_th = NULL;
        if (lp->nopening > 0) lp->nopening--;

        if (cancelled || !th) {
            if (!cancelled && !th)
                fprintf(stderr, "senkod: transport open failed (tproxy=%d)\n",
                        c->transparent);
            if (c->local_fd >= 0) close(c->local_fd);
            if (th && c->open_vt) c->open_vt->close(th);
            if (c->remote_fd >= 0) close(c->remote_fd);
            clear_conn_slot(c);
            continue;
        }

        c->th = th;
        sess_status_t ir;
        if (c->transparent) {
            ir = session_init(&c->sess, c->open_vt, th, c->open_proto,
                              c->open_proto == VL_PROTO_VLESS ? c->open_uuid : NULL,
                              c->open_flow[0] ? c->open_flow : NULL,
                              c->open_user[0] ? c->open_user : NULL,
                              c->open_pass[0] ? c->open_pass : NULL);
            if (ir == SESS_OK) {
                size_t pu = 0;
                ir = session_start_from_transparent_dest(&c->sess, &c->tproxy_dest,
                                                         c->prebuf, c->prebuf_len, &pu);
                if (ir == SESS_OK && pu > 0) {
                    memmove(c->prebuf, c->prebuf + pu, c->prebuf_len - pu);
                    c->prebuf_len -= pu;
                }
            }
        } else if (c->socks_greet_done) {
            ir = session_init_after_greet(&c->sess, c->open_vt, th, c->open_proto,
                                          c->open_proto == VL_PROTO_VLESS ? c->open_uuid : NULL,
                                          c->open_flow[0] ? c->open_flow : NULL,
                                          c->open_user[0] ? c->open_user : NULL,
                                          c->open_pass[0] ? c->open_pass : NULL);
        } else {
            ir = session_init(&c->sess, c->open_vt, th, c->open_proto,
                              c->open_proto == VL_PROTO_VLESS ? c->open_uuid : NULL,
                              c->open_flow[0] ? c->open_flow : NULL,
                              c->open_user[0] ? c->open_user : NULL,
                              c->open_pass[0] ? c->open_pass : NULL);
        }
        if (ir != SESS_OK) {
            if (c->open_vt) c->open_vt->close(th);
            if (c->remote_fd >= 0) close(c->remote_fd);
            if (c->local_fd >= 0) close(c->local_fd);
            clear_conn_slot(c);
            continue;
        }

        if (c->prebuf_len > 0) {
            size_t off = 0;
            while (off < c->prebuf_len) {
                size_t consumed = 0;
                if (session_feed_client(&c->sess, c->prebuf + off,
                                        c->prebuf_len - off, &consumed) != SESS_OK)
                    break;
                if (consumed == 0) break;
                off += consumed;
            }
            if (off > 0) {
                memmove(c->prebuf, c->prebuf + off, c->prebuf_len - off);
                c->prebuf_len -= off;
            }
            if (flush_to_local(c) != 0) {
                drop_conn(lp, c);
                continue;
            }
        }
        lp->nconns++;
    }
}

loop_status_t loop_set_server(loop_t *lp, const transport_vt_t *vt,
                              loop_dialer_fn dial, void *dial_ctx,
                              vl_proto_t proto,
                              const uint8_t uuid[VLESS_UUID_LEN], const char *flow,
                              const char *user, const char *pass,
                              const char *sni, const char *fingerprint,
                              const char *reality_pbk, const char *reality_sid,
                              const char *path, const char *ws_host,
                              const char *xhttp_mode) {
    if (!lp || !vt || !dial) return LOOP_ERR_ARG;
    if (proto == VL_PROTO_VLESS && !uuid) return LOOP_ERR_ARG;

    /* drop connections before switching servers */
    drop_all_conns(lp);

    lp->vt = vt;
    lp->dial = dial;
    lp->dial_ctx = dial_ctx;
    lp->proto = proto;
    if (uuid) {
        memcpy(lp->uuid, uuid, VLESS_UUID_LEN);
    } else {
        memset(lp->uuid, 0, sizeof lp->uuid);
    }
    copy_field(lp->flow, sizeof lp->flow, (flow && flow[0]) ? flow : NULL);
    copy_field(lp->user, sizeof lp->user, (user && user[0]) ? user : NULL);
    copy_field(lp->pass, sizeof lp->pass, (pass && pass[0]) ? pass : NULL);

    loop_set_tls(lp, sni, fingerprint, reality_pbk, reality_sid, path, ws_host,
                 xhttp_mode);

    lp->active = 1; /* accept clients again */
    return LOOP_OK;
}

void loop_stop(loop_t *lp) {
    if (!lp) return;
    drop_all_conns(lp);
    lp->active = 0; /* refuse new clients until a server is selected again */
}

loop_status_t loop_enable_tproxy(loop_t *lp, uint16_t port) {
    if (!lp || port == 0) return LOOP_ERR_ARG;
    loop_disable_tproxy(lp);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return LOOP_ERR_BIND;

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(fd, 32) != 0) {
        close(fd);
        return LOOP_ERR_BIND;
    }
    set_nonblock(fd);
    lp->tproxy_fd = fd;
    lp->tproxy_port = port;
    return LOOP_OK;
}

void loop_disable_tproxy(loop_t *lp) {
    if (!lp) return;
    if (lp->tproxy_fd >= 0) {
        close(lp->tproxy_fd);
        lp->tproxy_fd = -1;
        lp->tproxy_port = 0;
    }
    pf_natlook_close();
}

static void fill_open_fields(loop_t *lp, loop_conn_t *c) {
    c->open_vt = lp->vt;
    c->open_proto = lp->proto;
    memcpy(c->open_uuid, lp->uuid, sizeof c->open_uuid);
    copy_field(c->open_flow, sizeof c->open_flow, lp->flow[0] ? lp->flow : NULL);
    copy_field(c->open_user, sizeof c->open_user, lp->user[0] ? lp->user : NULL);
    copy_field(c->open_pass, sizeof c->open_pass, lp->pass[0] ? lp->pass : NULL);
    copy_field(c->open_sni, sizeof c->open_sni, lp->sni);
    copy_field(c->open_fingerprint, sizeof c->open_fingerprint, lp->fingerprint);
    copy_field(c->open_reality_pbk, sizeof c->open_reality_pbk, lp->reality_pbk);
    copy_field(c->open_reality_sid, sizeof c->open_reality_sid, lp->reality_sid);
    copy_field(c->open_path, sizeof c->open_path, lp->path);
    copy_field(c->open_ws_host, sizeof c->open_ws_host, lp->ws_host);
    copy_field(c->open_xhttp_mode, sizeof c->open_xhttp_mode, lp->xhttp_mode);
    c->open_tls_cfg.sni = c->open_sni;
    c->open_tls_cfg.fingerprint = c->open_fingerprint;
    c->open_tls_cfg.reality_pbk = c->open_reality_pbk;
    c->open_tls_cfg.reality_sid = c->open_reality_sid;
    c->open_tls_cfg.path = c->open_path;
    c->open_tls_cfg.ws_host = c->open_ws_host;
    c->open_tls_cfg.xhttp_mode = c->open_xhttp_mode;
}

static int start_opening(loop_t *lp, loop_conn_t *c, int cfd) {
    set_nonblock(cfd);
    /* the worker retries a failed first dial */
    int rfd = lp->dial(lp->dial_ctx);
    if (rfd >= 0) set_nonblock(rfd);

    c->local_fd = cfd;
    c->remote_fd = rfd;
    fill_open_fields(lp, c);
    c->used = 1;
    c->opening = 1;
    lp->nopening++;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, LOOP_OPEN_STACK_SZ);
    int cr = pthread_create(&c->open_thread, &attr, open_worker_main, c);
    pthread_attr_destroy(&attr);
    if (cr != 0) {
        lp->nopening--;
        if (c->remote_fd >= 0) close(c->remote_fd);
        if (c->local_fd >= 0) close(c->local_fd);
        clear_conn_slot(c);
        return -1;
    }
    return 0;
}

static void accept_one(loop_t *lp) {
    int cfd = accept(lp->listen_fd, NULL, NULL);
    if (cfd < 0) return;

    /* refuse clients until a server is selected */
    if (!lp->active || !lp->vt || !lp->dial) { close(cfd); return; }

    loop_conn_t *c = alloc_conn(lp);
    if (!c) {
        fprintf(stderr, "senkod: drop accept: conn cap\n");
        close(cfd);
        return;
    }
    if (lp->nopening >= LOOP_MAX_OPENING) {
        fprintf(stderr, "senkod: drop accept: opening cap %zu\n", lp->nopening);
        clear_conn_slot(c);
        close(cfd);
        return;
    }

    if (start_opening(lp, c, cfd) != 0)
        close(cfd);
}

static void accept_tproxy_one(loop_t *lp) {
    struct sockaddr_in clientaddr;
    socklen_t addrlen = sizeof clientaddr;
    int cfd = accept(lp->tproxy_fd, (struct sockaddr *)&clientaddr, &addrlen);
    if (cfd < 0) return;

    if (!lp->active || !lp->vt || !lp->dial) { close(cfd); return; }

    char host[256];
    uint16_t dport = 0;
    if (pf_natlook_dest(cfd, &clientaddr, lp->tproxy_port,
                         host, sizeof host, &dport) != 0) {
        close(cfd);
        return;
    }

    loop_conn_t *c = alloc_conn(lp);
    if (!c) {
        fprintf(stderr, "senkod: drop tproxy: conn cap\n");
        close(cfd);
        return;
    }
    if (lp->nopening >= LOOP_MAX_OPENING) {
        fprintf(stderr, "senkod: drop tproxy: opening cap %zu\n", lp->nopening);
        clear_conn_slot(c);
        close(cfd);
        return;
    }

    memset(&c->tproxy_dest, 0, sizeof c->tproxy_dest);
    c->tproxy_dest.port = dport;
    struct in_addr ia;
    if (inet_pton(AF_INET, host, &ia) == 1) {
        c->tproxy_dest.atyp = VLESS_ADDR_IPV4;
        memcpy(c->tproxy_dest.host_addr, &ia, sizeof ia);
    } else {
        c->tproxy_dest.atyp = VLESS_ADDR_DOMAIN;
        snprintf(c->tproxy_dest.domain, sizeof c->tproxy_dest.domain, "%s", host);
    }
    c->transparent = 1;

    if (start_opening(lp, c, cfd) != 0)
        close(cfd);
}

/* flush queued bytes without blocking */
static int flush_pend_local(loop_conn_t *c) {
    while (c->pend_off < c->pend_len) {
        ssize_t w = write(c->local_fd, c->pend + c->pend_off,
                          c->pend_len - c->pend_off);
        if (w > 0) { c->pend_off += (size_t)w; continue; }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        if (errno == EINTR) continue;
        return -1;
    }
    c->pend_off = c->pend_len = 0;
    return 0;
}

static int flush_to_local(loop_conn_t *c) {
    while (c->pend_off < c->pend_len) {
        ssize_t w = write(c->local_fd, c->pend + c->pend_off,
                          c->pend_len - c->pend_off);
        if (w > 0) { c->pend_off += (size_t)w; continue; }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; /* try later */
        if (errno == EINTR) continue;
        return -1;
    }
    c->pend_off = c->pend_len = 0; /* fully drained, reset */

    for (;;) {
        size_t n = session_take_client(&c->sess, c->pend, sizeof c->pend);
        if (n == 0) return 0; /* session has nothing more */
        c->pend_len = n;
        c->pend_off = 0;
        while (c->pend_off < c->pend_len) {
            ssize_t w = write(c->local_fd, c->pend + c->pend_off,
                              c->pend_len - c->pend_off);
            if (w > 0) { c->pend_off += (size_t)w; continue; }
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; /* keep pend */
            if (errno == EINTR) continue;
            return -1;
        }
        c->pend_off = c->pend_len = 0;
    }
}

static void service_conn(loop_t *lp, loop_conn_t *c,
                         short local_re, short remote_re) {
/* pump remote output when it is ready */
    if (remote_re & (POLLIN | POLLOUT | POLLHUP | POLLERR)) {
        session_pump_remote(&c->sess);
    }

    if (local_re & POLLIN) {
        uint8_t buf[8192];
        ssize_t n = read(c->local_fd, buf, sizeof buf);
        if (n > 0) {
            size_t off = 0;
            while (off < (size_t)n) {
                size_t consumed = 0;
                sess_status_t fr = session_feed_client(&c->sess, buf + off,
                                                       (size_t)n - off, &consumed);
                if (fr != SESS_OK) {
                    if (c->sess.state == SESS_ERROR) {
                        drop_conn(lp, c);
                        return;
                    }
                    break;
                }
                if (consumed == 0) { /* session backpressured */
                    session_pump_remote(&c->sess);
                    break;
                }
                off += consumed;
            }
        } else if (n == 0) {
            c->sess.state = (c->sess.state == SESS_RELAY) ? SESS_CLOSED : c->sess.state;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            drop_conn(lp, c);
            return;
        }
    }
    /* flush before handling hangup */
    if (flush_to_local(c) != 0) { drop_conn(lp, c); return; }

    if (local_re & (POLLHUP | POLLERR)) {
        drop_conn(lp, c);
        return;
    }

    /* close only after the session and pending output are done */
    if (session_is_done(&c->sess) && c->pend_off >= c->pend_len)
        drop_conn(lp, c);
}

loop_status_t loop_step(loop_t *lp, int timeout_ms) {
    if (!lp) return LOOP_ERR_ARG;
    reap_opening_conns(lp);

    struct pollfd pfd[3 + 2 * LOOP_MAX_CONNS];
    loop_conn_t *map[3 + 2 * LOOP_MAX_CONNS]; /* fd to conn map */
    int is_remote[3 + 2 * LOOP_MAX_CONNS];

    nfds_t nf = 0;
    pfd[nf].fd = lp->listen_fd;
    pfd[nf].events = POLLIN;
    map[nf] = NULL; is_remote[nf] = 0;
    nf++;

    int tproxy_idx = -1;
    if (lp->tproxy_fd >= 0) {
        tproxy_idx = (int)nf;
        pfd[nf].fd = lp->tproxy_fd;
        pfd[nf].events = POLLIN;
        map[nf] = NULL; is_remote[nf] = 0;
        nf++;
    }

    nfds_t wake_idx = nf;
    pfd[nf].fd = lp->wake_rd;
    pfd[nf].events = POLLIN;
    map[nf] = NULL; is_remote[nf] = 0;
    nf++;

    nfds_t conn_base = nf;

    for (size_t i = 0; i < LOOP_MAX_CONNS; ++i) {
        loop_conn_t *c = &lp->conns[i];
        if (!c->used) continue;

        if (c->opening) {
        /* wake the loop after cancellation */
            if (c->local_fd < 0) continue;
            short lev = POLLIN;
            if (c->pend_off < c->pend_len) lev |= POLLOUT;
            pfd[nf].fd = c->local_fd;
            pfd[nf].events = lev;
            map[nf] = c; is_remote[nf] = 0;
            nf++;
            continue;
        }

        if (c->sess.state == SESS_VISION_FIRST)
            session_pump_remote(&c->sess);

/* poll the local socket and pending output */
        short lev = POLLIN;
        if (c->pend_off < c->pend_len) lev |= POLLOUT;
        pfd[nf].fd = c->local_fd;
        pfd[nf].events = lev;
        map[nf] = c; is_remote[nf] = 0;
        nf++;

        /* poll remote writes only when needed */
        short ev = POLLIN;
        if (c->sess.to_remote_len > 0) ev |= POLLOUT;
        else if (c->sess.vt && c->sess.vt->want_write && c->sess.th &&
                 c->sess.vt->want_write(c->sess.th))
            ev |= POLLOUT;
        pfd[nf].fd = c->remote_fd;
        pfd[nf].events = ev;
        map[nf] = c; is_remote[nf] = 1;
        nf++;
    }

    int r = poll(pfd, nf, timeout_ms);
    if (r < 0) {
        if (errno == EINTR) return LOOP_OK;
        return LOOP_ERR;
    }
    if (r == 0) return LOOP_OK; /* timeout, nothing to do */

    if (pfd[0].revents & POLLIN) accept_one(lp);
    if (tproxy_idx >= 0 && (pfd[tproxy_idx].revents & POLLIN))
        accept_tproxy_one(lp);
    if (pfd[wake_idx].revents & POLLIN) {
        drain_wake(lp);
        reap_opening_conns(lp);
    }

    /* process connections after accepting new clients */
    for (nfds_t i = conn_base; i < nf; ++i) {
        loop_conn_t *c = map[i];
        if (!c || !c->used) continue;

        if (c->opening) {
            service_opening_local(lp, c, pfd[i].revents);
            continue;
        }

        short local_re = 0, remote_re = 0;
        if (is_remote[i]) remote_re = pfd[i].revents;
        else              local_re  = pfd[i].revents;
        for (nfds_t j = conn_base; j < nf; ++j) {
            if (j == i || map[j] != c) continue;
            if (is_remote[j]) remote_re |= pfd[j].revents;
            else              local_re  |= pfd[j].revents;
        }

        service_conn(lp, c, local_re, remote_re);

        for (nfds_t j = conn_base; j < nf; ++j) {
            if (map[j] == c) map[j] = NULL;
        }
    }
    reap_opening_conns(lp);
    return LOOP_OK;
}

size_t loop_conn_count(const loop_t *lp) {
    return lp ? lp->nconns : 0;
}

void loop_close(loop_t *lp) {
    if (!lp) return;
    for (size_t i = 0; i < LOOP_MAX_CONNS; ++i) {
        if (lp->conns[i].used) drop_conn(lp, &lp->conns[i]);
    }
    for (size_t i = 0; i < LOOP_MAX_CONNS; ++i) {
        loop_conn_t *c = &lp->conns[i];
        if (!c->used || !c->opening) continue;
        pthread_join(c->open_thread, NULL);
        if (c->open_th && c->open_vt) c->open_vt->close(c->open_th);
        if (c->remote_fd >= 0) close(c->remote_fd);
        if (c->local_fd >= 0) close(c->local_fd);
        if (lp->nopening > 0) lp->nopening--;
        clear_conn_slot(c);
    }
    loop_disable_tproxy(lp);
    if (lp->listen_fd >= 0) close(lp->listen_fd);
    if (lp->wake_rd >= 0) close(lp->wake_rd);
    if (lp->wake_wr >= 0) close(lp->wake_wr);
    if (lp->open_lock_ready) pthread_mutex_destroy(&lp->open_lock);
    lp->listen_fd = -1;
    lp->wake_rd = lp->wake_wr = -1;
    lp->open_lock_ready = 0;
}
