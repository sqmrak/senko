#include "real_probe.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "senko_clock.h"
#include "senko_time.h"
#include "session.h"
#include "transport_pick.h"
#include "vless.h"

/* vision holds its first write for a short bootstrap, and the session only
   notices the deadline when it is pumped */
#define REAL_PROBE_TICK_MS 50
/* the 204 status line and headers, Date among them, fit well inside this */
#define REAL_PROBE_HEAD_MAX 1024

static void set_reason(char *reason, size_t cap, const char *text) {
    if (reason && cap) snprintf(reason, cap, "%s", text);
}

int real_probe_status(const char *buf, size_t len) {
    return net_fail_http_status(buf, len);
}

static int wait_fd(int fd, short events, int64_t deadline) {
    for (;;) {
        int64_t left = deadline - senko_now_ms();
        if (left <= 0) return 0;
        struct pollfd p;
        p.fd = fd;
        p.events = events;
        p.revents = 0;
        int rc = poll(&p, 1, left > REAL_PROBE_TICK_MS ? REAL_PROBE_TICK_MS : (int)left);
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0) return -1;
        if (rc > 0) return 1;
        return 0;
    }
}

/* a connected nonblocking socket, or -1 with fail saying why the connect did
   not complete */
static int dial_ipv4(const char *ip, uint16_t port, real_probe_bind_fn bind_fd,
                     int64_t deadline, net_fail_t *fail) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (!ip || inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        net_fail_note(fail, NET_FAIL_SYSTEM, EINVAL);
        return -1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        net_fail_note(fail, NET_FAIL_SYSTEM, errno);
        return -1;
    }
    if (bind_fd) bind_fd(fd);
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        close(fd);
        net_fail_note(fail, NET_FAIL_SYSTEM, errno);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0 && errno != EINPROGRESS) {
        net_fail_note(fail, net_fail_from_errno(errno), 0);
        close(fd);
        return -1;
    }
    while (senko_now_ms() < deadline) {
        int ready = wait_fd(fd, POLLOUT, deadline);
        if (ready < 0) break;
        if (ready == 0) continue;
        int soerr = 0;
        socklen_t len = sizeof soerr;
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) != 0 || soerr != 0) {
            net_fail_note(fail, net_fail_from_errno(soerr ? soerr : errno), 0);
            close(fd);
            return -1;
        }
        return fd;
    }
    net_fail_note(fail, NET_FAIL_TIMEOUT, 0);
    close(fd);
    return -1;
}

static void finish_text(net_diag_t *diag, char *reason, size_t cap) {
    diag->clock_known = senko_clock_offset(&diag->clock_offset_s);
    if (reason && cap) net_diag_text(diag, reason, cap);
}

/* why a session that stopped early stopped: the transport's own failure is
   the most precise, then what the session saw in the answer, then the close */
static void classify_stop(net_diag_t *diag, const net_fail_t *fail, const session_t *s,
                          net_diag_stage_t otherwise) {
    if (fail->kind != NET_FAIL_NONE) {
        diag->stage = NET_DIAG_TRANSPORT;
        diag->transport = *fail;
    } else if (s && s->fail != SESS_FAIL_NONE) {
        diag->stage = NET_DIAG_ANSWER;
        diag->answer = s->fail;
    } else {
        diag->stage = otherwise;
    }
}

int real_probe_run(const vl_server_t *server, const char *ip,
                   real_probe_bind_fn bind_fd, int timeout_ms,
                   net_diag_t *diag_out, char *reason, size_t reason_cap) {
    static const char request[] =
        "GET /generate_204 HTTP/1.1\r\n"
        "Host: " REAL_PROBE_HOST "\r\n"
        "User-Agent: senko\r\n"
        "Connection: close\r\n\r\n";
    const size_t request_len = sizeof request - 1;
    net_diag_t local;
    net_diag_t *diag = diag_out ? diag_out : &local;
    memset(diag, 0, sizeof *diag);

    set_reason(reason, reason_cap, "");
    if (!server || !ip || timeout_ms <= 0) {
        set_reason(reason, reason_cap, "bad probe arguments");
        return -1;
    }
    diag->proto = server->proto;
    diag->port = server->port;
    const transport_vt_t *vt = transport_for_server(server);
    if (!vt) {
        set_reason(reason, reason_cap, "unsupported transport");
        return -1;
    }
    uint8_t uuid[VLESS_UUID_LEN];
    memset(uuid, 0, sizeof uuid);
    if (cfg_proto_uses_uuid(server->proto) &&
        vless_uuid_parse(server->uuid, uuid) != VLESS_OK) {
        set_reason(reason, reason_cap, "invalid uuid");
        return -1;
    }

    net_fail_t fail;
    memset(&fail, 0, sizeof fail);
    int64_t start = senko_now_ms();
    int64_t deadline = start + timeout_ms;
    int fd = dial_ipv4(ip, server->port, bind_fd, deadline, &fail);
    if (fd < 0) {
        classify_stop(diag, &fail, NULL, NET_DIAG_TRANSPORT);
        finish_text(diag, reason, reason_cap);
        return -1;
    }

    transport_tls_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.sni = server->sni;
    cfg.fingerprint = server->fp;
    cfg.reality_pbk = server->pbk;
    cfg.reality_sid = server->sid;
    cfg.path = server->path;
    cfg.ws_host = server->ws_host;
    cfg.xhttp_mode = server->mode;
    cfg.peer_host = server->host;
    cfg.insecure = server->insecure;
    cfg.fail = &fail;
    void *th = vt->open(fd, &cfg);
    if (!th) {
        close(fd);
        net_fail_note(&fail, NET_FAIL_TRANSPORT, 0);
        classify_stop(diag, &fail, NULL, NET_DIAG_TRANSPORT);
        finish_text(diag, reason, reason_cap);
        return -1;
    }

    int ms = -1;
    session_t *s = (session_t *)calloc(1, sizeof *s);
    if (!s) {
        set_reason(reason, reason_cap, "out of memory");
        goto done;
    }
    if (session_init(s, vt, th, server->proto,
                     cfg_proto_uses_uuid(server->proto) ? uuid : NULL,
                     server->flow[0] ? server->flow : NULL,
                     server->user[0] ? server->user : NULL,
                     server->pass[0] ? server->pass : NULL) != SESS_OK) {
        set_reason(reason, reason_cap, "protocol session could not start");
        goto done;
    }

    vless_dest_t dest;
    memset(&dest, 0, sizeof dest);
    dest.atyp = VLESS_ADDR_DOMAIN;
    snprintf(dest.domain, sizeof dest.domain, "%s", REAL_PROBE_HOST);
    dest.port = REAL_PROBE_PORT;
    size_t fed = 0;
    if (session_start_from_transparent_dest(s, &dest, (const uint8_t *)request,
                                            request_len, &fed) != SESS_OK) {
        set_reason(reason, reason_cap, "proxy request could not be encoded");
        goto done;
    }

    char answer[REAL_PROBE_HEAD_MAX + 1] = {0};
    size_t answer_len = 0;
    for (;;) {
        if (senko_now_ms() >= deadline) {
            classify_stop(diag, &fail, s, NET_DIAG_SILENT);
            finish_text(diag, reason, reason_cap);
            goto done;
        }
        if (fed < request_len) {
            size_t consumed = 0;
            if (session_feed_client(s, (const uint8_t *)request + fed,
                                    request_len - fed, &consumed) != SESS_OK) {
                classify_stop(diag, &fail, s, NET_DIAG_CLOSED);
                finish_text(diag, reason, reason_cap);
                goto done;
            }
            fed += consumed;
        }
        sess_status_t pumped = session_pump_remote(s);
        uint8_t chunk[256];
        size_t n = session_take_client(s, chunk, sizeof chunk);
        if (n > 0 && answer_len < REAL_PROBE_HEAD_MAX) {
            size_t copy = REAL_PROBE_HEAD_MAX - answer_len;
            if (copy > n) copy = n;
            memcpy(answer + answer_len, chunk, copy);
            answer_len += copy;
            answer[answer_len] = '\0';
        }
        int head_done = strstr(answer, "\r\n\r\n") != NULL;
        int ended = pumped != SESS_OK || s->state == SESS_ERROR || s->state == SESS_CLOSED;
        if (head_done) {
            int status = real_probe_status(answer, answer_len);
            int64_t server_time = 0;
            if (status == 204 && senko_clock_from_http_head(answer, answer_len, &server_time) == 0)
                senko_clock_note(server_time);
            if (status == 204) {
                ms = (int)(senko_now_ms() - start);
                goto done;
            }
            /* an unexpected status can be a fallback site, a portal or a failed
               check target; it does not prove bad credentials */
            if (status > 0) {
                diag->stage = NET_DIAG_NOT_PROXIED;
                diag->http_status = status;
            } else {
                diag->stage = NET_DIAG_ANSWER;
                diag->answer = SESS_FAIL_PROTOCOL;
            }
            finish_text(diag, reason, reason_cap);
            goto done;
        }
        if (ended || answer_len == REAL_PROBE_HEAD_MAX) {
            if (answer_len) {
                diag->stage = NET_DIAG_ANSWER;
                diag->answer = SESS_FAIL_PROTOCOL;
            } else {
                classify_stop(diag, &fail, s, NET_DIAG_CLOSED);
            }
            finish_text(diag, reason, reason_cap);
            goto done;
        }
        short events = POLLIN;
        if ((s->to_remote_len > 0 && !s->to_remote_wait_read) ||
            (vt->want_write && vt->want_write(th)))
            events |= POLLOUT;
        if (wait_fd(fd, events, deadline) < 0) {
            classify_stop(diag, &fail, s, NET_DIAG_CLOSED);
            finish_text(diag, reason, reason_cap);
            goto done;
        }
    }

done:
    vt->close(th);
    close(fd);
    free(s);
    return ms;
}

int real_probe_tcp(const vl_server_t *server, const char *ip,
                   real_probe_bind_fn bind_fd, int timeout_ms,
                   char *reason, size_t reason_cap) {
    set_reason(reason, reason_cap, "");
    if (!server || !ip || timeout_ms <= 0) {
        set_reason(reason, reason_cap, "bad probe arguments");
        return -1;
    }
    int64_t start = senko_now_ms();
    net_fail_t fail = {0};
    int fd = dial_ipv4(ip, server->port, bind_fd, start + timeout_ms, &fail);
    if (fd < 0) {
        net_diag_t diag = {0};
        diag.stage = NET_DIAG_TRANSPORT;
        diag.proto = server->proto;
        diag.port = server->port;
        diag.transport = fail;
        net_diag_text(&diag, reason, reason_cap);
        return -1;
    }
    close(fd);
    return (int)(senko_now_ms() - start);
}

int real_probe_clock(const char *ip, real_probe_bind_fn bind_fd, int timeout_ms) {
    static const char request[] =
        "GET /generate_204 HTTP/1.1\r\n"
        "Host: " REAL_PROBE_HOST "\r\n"
        "User-Agent: senko\r\n"
        "Connection: close\r\n\r\n";
    if (!ip || timeout_ms <= 0) return -1;
    int64_t deadline = senko_now_ms() + timeout_ms;
    net_fail_t fail;
    memset(&fail, 0, sizeof fail);
    int fd = dial_ipv4(ip, REAL_PROBE_PORT, bind_fd, deadline, &fail);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < sizeof request - 1) {
        ssize_t w = write(fd, request + off, sizeof request - 1 - off);
        if (w > 0) {
            off += (size_t)w;
            continue;
        }
        if (w < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) &&
            wait_fd(fd, POLLOUT, deadline) > 0)
            continue;
        close(fd);
        return -1;
    }
    char head[REAL_PROBE_HEAD_MAX + 1] = {0};
    size_t got = 0;
    int rc = -1;
    while (got < REAL_PROBE_HEAD_MAX) {
        ssize_t r = read(fd, head + got, REAL_PROBE_HEAD_MAX - got);
        if (r > 0) {
            got += (size_t)r;
            head[got] = '\0';
            if (strstr(head, "\r\n\r\n")) break;
            continue;
        }
        if (r < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) &&
            wait_fd(fd, POLLIN, deadline) > 0)
            continue;
        break;
    }
    int64_t server_time = 0;
    /* a captive portal answers this host too, with its own clock or none */
    if (strstr(head, "\r\n\r\n") && real_probe_status(head, got) == 204 &&
        senko_clock_from_http_head(head, got, &server_time) == 0) {
        senko_clock_note(server_time);
        rc = 0;
    }
    close(fd);
    return rc;
}
