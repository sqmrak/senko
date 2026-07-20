#define _DEFAULT_SOURCE /* getaddrinfo needs the default feature set */

#include "daemon_ctl.h"
#include "storefile.h"
#include "vpn_icon.h"

#include "core/transport.h"
#include "core/transport_pick.h"
#include "core/vless.h"
#include "core/subfetch.h"
#include "core/net_safe.h"
#include "core/url.h"
#include "core/tls_clienthello.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <openssl/rand.h>

void daemon_ctl_init(daemon_ctl_t *d, loop_t *loop, const char *config_path) {
    if (!d) return;
    memset(d, 0, sizeof *d);
    d->loop = loop;
    if (config_path) {
        size_t l = strlen(config_path);
        if (l >= sizeof d->config_path) l = sizeof d->config_path - 1;
        memcpy(d->config_path, config_path, l);
        d->config_path[l] = '\0';
    }
}

void daemon_ctl_set_full_device(daemon_ctl_t *d, int on) {
    if (d) d->full_device = on ? 1 : 0;
}

void daemon_ctl_set_settings(daemon_ctl_t *d, const daemon_settings_t *s) {
    if (!d || !s) return;
    d->settings = *s;
}

void daemon_ctl_shutdown(daemon_ctl_t *d) {
    if (!d) return;
    vpn_icon_set(0);
/* remove routing before stopping */
    if (d->full_device) {
        loop_disable_tproxy(d->loop);
        routing_exec_down(&d->routing);
    }
}

static int ipv4_list_contains(const char *list, const char *ip) {
    size_t ip_len = strlen(ip);
    const char *p = list;
    while (p && *p) {
        while (*p == ' ' || *p == ',') ++p;
        const char *item_end = strchr(p, ',');
        if (!item_end) item_end = p + strlen(p);
        while (item_end > p && item_end[-1] == ' ') --item_end;
        if ((size_t)(item_end - p) == ip_len && memcmp(p, ip, ip_len) == 0)
            return 1;
        p = *item_end ? item_end + 1 : item_end;
    }
    return 0;
}

static int ipv4_list_append(char *list, size_t cap, const char *ip) {
    if (!list || cap == 0 || ipv4_list_contains(list, ip)) return 0;
    size_t used = strlen(list);
    int n = snprintf(list + used, cap - used, "%s%s", used ? ", " : "", ip);
    return n < 0 || (size_t)n >= cap - used ? -1 : 0;
}

static int resolve_ipv4_addresses(const char *host, char *first_ip, size_t first_cap,
                                  char *ip_list, size_t list_cap,
                                  int reject_unsafe) {
    if (!host || !first_ip || first_cap == 0) return -1;
    first_ip[0] = '\0';
    if (ip_list && list_cap) ip_list[0] = '\0';

    struct in_addr literal;
    if (inet_aton(host, &literal)) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof addr);
        addr.sin_family = AF_INET;
        addr.sin_addr = literal;
        if (reject_unsafe && !net_addr_allowed((struct sockaddr *)&addr)) return -1;
        char ip[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &literal, ip, sizeof ip)) return -1;
        int n = snprintf(first_ip, first_cap, "%s", ip);
        if (n < 0 || (size_t)n >= first_cap) return -1;
        return ipv4_list_append(ip_list, list_cap, ip);
    }

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; /* routing rules here are ipv4 only */
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return -1;

    int ok = 0;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET ||
            (reject_unsafe && !net_addr_allowed(ai->ai_addr))) continue;
        char ip[INET_ADDRSTRLEN];
        struct sockaddr_in *sin = (struct sockaddr_in *)ai->ai_addr;
        if (!inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof ip)) continue;
        if (!first_ip[0]) {
            int n = snprintf(first_ip, first_cap, "%s", ip);
            if (n < 0 || (size_t)n >= first_cap) {
                ok = -1;
                break;
            }
        }
        if (ipv4_list_append(ip_list, list_cap, ip) != 0) {
            ok = -1;
            break;
        }
        ok = 1;
    }
    freeaddrinfo(res);
    return ok > 0 && first_ip[0] ? 0 : -1;
}

int daemon_ctl_apply(void *ctx, const ctl_action_t *action) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (!d || !d->loop || !action) return -1;

    switch (action->kind) {
        case CTL_ACT_START: {
            const vl_server_t *s = &action->server;
            /* show the VPN icon only after the tunnel passes verification */

            const transport_vt_t *vt = transport_for_server(s);
            if (!vt) {
                fprintf(stderr, "senkod: unsupported transport/security for server\n");
                vpn_icon_set(0);
                return DCTL_ERR_TRANSPORT;
            }

            uint8_t uuid[VLESS_UUID_LEN];
            memset(uuid, 0, sizeof uuid);
            if (s->proto == VL_PROTO_VLESS) {
                if (vless_uuid_parse(s->uuid, uuid) != VLESS_OK) {
                    fprintf(stderr, "senkod: bad uuid in server link\n");
                    vpn_icon_set(0);
                    return DCTL_ERR_UUID;
                }
            }

            /* clear routing before switching servers */
            if (d->full_device && d->routing.mode != ROUTING_MODE_NONE) {
                loop_disable_tproxy(d->loop);
                routing_exec_down(&d->routing);
            }

            dialer_set_target(&d->dialer, s->host, s->port);

            /* point the loop at the new server */
            if (loop_set_server(d->loop, vt, dialer_connect, &d->dialer,
                                s->proto, uuid, s->flow, s->user, s->pass,
                                s->sni, s->fp, s->pbk, s->sid, s->path,
                                s->ws_host, s->mode) != LOOP_OK) {
                fprintf(stderr, "senkod: socks listener failed\n");
                vpn_icon_set(0);
                return DCTL_ERR_LOOP;
            }

            /* set full-device routing after SOCKS is ready */
            if (d->full_device) {
                char first_ip[64], ip_list[4096];
                first_ip[0] = '\0';
                ip_list[0] = '\0';

                if (resolve_ipv4_addresses(s->host, first_ip, sizeof first_ip,
                                           ip_list, sizeof ip_list, 0) != 0) {
                    fprintf(stderr, "senkod: dns resolution failed for %s\n", s->host);
                    loop_stop(d->loop);
                    vpn_icon_set(0);
                    return DCTL_ERR_DNS;
                }

                if (routing_exec_up(&d->routing, loop_listen_port(d->loop),
                                    first_ip, ip_list,
                                    d->settings.dns_upstream,
                                    (int)d->settings.dns_local_port) != REXEC_OK) {
                    fprintf(stderr, "senkod: routing setup failed\n");
                    loop_stop(d->loop);
                    vpn_icon_set(0);
                    return DCTL_ERR_ROUTING;
                }
                if (d->routing.use_internal_tproxy) {
                    if (loop_enable_tproxy(d->loop,
                            (uint16_t)d->routing.redir_port) != LOOP_OK) {
                        fprintf(stderr, "senkod: transparent listener failed\n");
                        routing_exec_down(&d->routing);
                        loop_stop(d->loop);
                        vpn_icon_set(0);
                        return DCTL_ERR_ROUTING;
                    }
                    fprintf(stderr, "senkod: transparent tcp on 127.0.0.1:%d\n",
                            d->routing.redir_port);
                }
            }
            return 0;
        }

        case CTL_ACT_STOP:
            vpn_icon_set(0);
            /* stop routing before closing the loop */
            if (d->full_device) {
                loop_disable_tproxy(d->loop);
                routing_exec_down(&d->routing);
            }
            loop_stop(d->loop);
            return 0;

        case CTL_ACT_PING:
/* ping is handled by the control path */
            return 0;

        case CTL_ACT_REFRESH:
/* refresh is handled by ctl_server */
            return -1;

        case CTL_ACT_NONE:
        default:
            return 0;
    }
}

void daemon_ctl_persist(void *ctx, const store_t *store) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (!d || !store || !d->config_path[0]) return;
    storefile_save(store, &d->settings, d->config_path);
}

static void subfetch_pump_loop(void *ctx) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (d && d->loop) loop_step(d->loop, 0);
}

/* monotonic clock for timeouts */
static long probe_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static int write_all_pumped_until(int fd, const void *buf, size_t len,
                                  daemon_ctl_t *d, long deadline_ms) {
    size_t off = 0;
    while (off < len) {
        if (probe_now_ms() >= deadline_ms) return -1;
        subfetch_pump_loop(d);
        ssize_t w = write(fd, (const uint8_t *)buf + off, len - off);
        if (w > 0) { off += (size_t)w; continue; }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pfd = { fd, POLLOUT, 0 };
            poll(&pfd, 1, 10);
            continue;
        }
        return -1;
    }
    return 0;
}

static int read_full_pumped_until(int fd, void *buf, size_t len,
                                  daemon_ctl_t *d, long deadline_ms) {
    size_t off = 0;
    while (off < len) {
        if (probe_now_ms() >= deadline_ms) return -1;
        subfetch_pump_loop(d);
        ssize_t r = read(fd, (uint8_t *)buf + off, len - off);
        if (r > 0) { off += (size_t)r; continue; }
        if (r == 0) return -1;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            struct pollfd pfd = { fd, POLLIN, 0 };
            poll(&pfd, 1, 10);
            continue;
        }
        if (errno == EINTR) continue;
        return -1;
    }
    return 0;
}

/* send DNS and probes through the local SOCKS loop */
static int socks5_dial_via_loop(daemon_ctl_t *d, uint16_t socks_port,
                                  const char *host, uint16_t port,
                                  long deadline_ms, const char **stage_out) {
    if (stage_out) *stage_out = "socks dial failed";
    if (!net_hostname_safe(host)) return -1;
    size_t hlen = strlen(host);
    if (hlen > 255) return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(socks_port);
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    int cr = connect(fd, (struct sockaddr *)&addr, sizeof addr);
    if (cr < 0 && errno != EINPROGRESS) {
        if (stage_out) *stage_out = "socks listen connect failed";
        close(fd);
        return -1;
    }
    if (cr < 0) {
/* require readiness so verify never writes into a timed-out socket */
        int ready = 0;
        while (probe_now_ms() < deadline_ms) {
            subfetch_pump_loop(d);
            struct pollfd pfd = { fd, POLLOUT, 0 };
            if (poll(&pfd, 1, 10) <= 0) continue;
            int soerr = 0;
            socklen_t sl = sizeof soerr;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0) {
                if (stage_out) *stage_out = "socks listen connect failed";
                close(fd);
                return -1;
            }
            ready = 1;
            break;
        }
        if (!ready) {
            if (stage_out) *stage_out = "socks listen connect timeout";
            close(fd);
            return -1;
        }
    }

    uint8_t greet[] = { 0x05, 0x01, 0x00 };
    if (write_all_pumped_until(fd, greet, 3, d, deadline_ms) != 0) {
        if (stage_out) *stage_out = "socks greet write failed";
        close(fd);
        return -1;
    }
    uint8_t gresp[2];
    if (read_full_pumped_until(fd, gresp, 2, d, deadline_ms) != 0 ||
        gresp[0] != 0x05 || gresp[1] != 0x00) {
        if (stage_out) *stage_out = "socks greet failed";
        close(fd);
        return -1;
    }

    uint8_t req[4 + 256 + 2];
    size_t n = 0;
    req[n++] = 0x05;
    req[n++] = 0x01;
    req[n++] = 0x00;
    req[n++] = 0x03; /* domain name */
    req[n++] = (uint8_t)hlen;
    memcpy(req + n, host, hlen);
    n += hlen;
    req[n++] = (uint8_t)(port >> 8);
    req[n++] = (uint8_t)(port & 0xff);
    if (write_all_pumped_until(fd, req, n, d, deadline_ms) != 0) {
        if (stage_out) *stage_out = "socks request write failed";
        close(fd);
        return -1;
    }

    uint8_t rhdr[4];
    if (read_full_pumped_until(fd, rhdr, 4, d, deadline_ms) != 0) {
/* open worker failed / peer closed before socks reply */
        if (stage_out) *stage_out = "tunnel open failed";
        close(fd);
        return -1;
    }
    if (rhdr[0] != 0x05 || rhdr[1] != 0x00) {
        if (stage_out) *stage_out = "tunnel open failed";
        close(fd);
        return -1;
    }
    size_t tail = 0;
    if (rhdr[3] == 0x01) tail = 6;
    else if (rhdr[3] == 0x04) tail = 18;
    else if (rhdr[3] == 0x03) {
        uint8_t dlen;
        if (read_full_pumped_until(fd, &dlen, 1, d, deadline_ms) != 0) {
            if (stage_out) *stage_out = "socks reply truncated";
            close(fd);
            return -1;
        }
        tail = (size_t)dlen + 2;
    } else {
        if (stage_out) *stage_out = "socks reply bad atyp";
        close(fd);
        return -1;
    }
    uint8_t junk[260];
    while (tail > 0) {
        size_t chunk = tail > sizeof junk ? sizeof junk : tail;
        if (read_full_pumped_until(fd, junk, chunk, d, deadline_ms) != 0) {
            if (stage_out) *stage_out = "socks reply truncated";
            close(fd);
            return -1;
        }
        tail -= chunk;
    }

    if (stage_out) *stage_out = NULL;
    return fd;
}

/* prefer ipv4 because broken ios6 v6 can make every connect fail */
static int subfetch_dial_direct(const char *host, uint16_t port) {
    char portstr[8];
    snprintf(portstr, sizeof portstr, "%u", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        if (!net_addr_allowed(ai->ai_addr)) continue;
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            int fl = fcntl(fd, F_GETFL, 0); /* subfetch needs nonblocking */
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
            break;
        }
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* prefer tunnel when full-device is up */
static int subfetch_dial(void *ctx, const char *host, uint16_t port) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (d && d->full_device && d->routing.mode != ROUTING_MODE_NONE && d->loop) {
        uint16_t sp = loop_listen_port(d->loop);
        if (sp) {
            long deadline = probe_now_ms() + 8000;
            int fd = socks5_dial_via_loop(d, sp, host, port, deadline, NULL);
            if (fd >= 0) return fd;
        }
/* try direct after tunnel failure so refresh can recover a dead node */
    }
    return subfetch_dial_direct(host, port);
}

int daemon_ctl_fetch(void *ctx, const char *url,
                     unsigned char *buf, size_t cap, size_t *len) {
    subfetch_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.dial = subfetch_dial;
    cfg.dial_ctx = ctx;
    cfg.pump = subfetch_pump_loop;
    cfg.pump_ctx = ctx;
    cfg.tcp = &transport_tcp;
    cfg.tls = &transport_tls; /* https subs use tls */
    cfg.max_redirects = 5;

    subfetch_status_t r = subfetch_get(&cfg, url, buf, cap, len, 15000);
    if (r != SUBFETCH_OK) {
        const char *why = "unknown";
        switch (r) {
            case SUBFETCH_ERR_ARG:       why = "bad arg"; break;
            case SUBFETCH_ERR_URL:        why = "bad url"; break;
            case SUBFETCH_ERR_DIAL:       why = "dial/dns"; break;
            case SUBFETCH_ERR_TRANSPORT: why = "tls/io"; break;
            case SUBFETCH_ERR_HTTP:       why = "http status/body"; break;
            case SUBFETCH_ERR_TOOBIG:     why = "body too big"; break;
            case SUBFETCH_ERR_REDIRECT:   why = "redirect"; break;
            default: break;
        }
/* sub urls often carry tokens in path/query; keep those out of logs */
        url_t u;
        if (url && url_parse(url, &u) == URL_OK)
            fprintf(stderr, "senkod: subfetch failed: %s (rc=%d) %s://%s:%u/...\n",
                    why, (int)r, u.is_https ? "https" : "http", u.host, (unsigned)u.port);
        else
            fprintf(stderr, "senkod: subfetch failed: %s (rc=%d)\n", why, (int)r);
        return -1;
    }
    fprintf(stderr, "senkod: subfetch ok %zu bytes\n", len ? *len : 0);
    return 0;
}

static int probe_tcp_ipv4(daemon_ctl_t *d, const char *ip, uint16_t port,
                          int timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) { close(fd); return -1; }

    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);

/* exclude dns because this probe measures tcp connect latency */
    long start = probe_now_ms();
    long deadline = start + timeout_ms;
    int r = connect(fd, (struct sockaddr *)&addr, sizeof addr);
    if (r == 0) { close(fd); return (int)(probe_now_ms() - start); }
    if (errno != EINPROGRESS) { close(fd); return -1; }

    for (;;) {
        if (d) subfetch_pump_loop(d);
        long now = probe_now_ms();
        if (now >= deadline) { close(fd); return -1; }

        int remain = (int)(deadline - now);
/* use short slices so displayed rtt does not jump in coarse steps */
        int slice = remain > 5 ? 5 : remain;
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, slice);
        if (pr < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        if (pr == 0) continue;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            close(fd);
            return -1;
        }
        if (!(pfd.revents & POLLOUT)) continue;

        int soerr = 0;
        socklen_t sl = sizeof soerr;
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0) {
            close(fd);
            return -1;
        }
        close(fd);
        return (int)(probe_now_ms() - start);
    }
}

int daemon_ctl_probe(void *ctx, const char *host, uint16_t port) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    const int timeout_ms = 1500; /* per try; two tries stay under senkoctl 5s */

    char ip[INET_ADDRSTRLEN];
    if (resolve_ipv4_addresses(host, ip, sizeof ip, NULL, 0, 1) != 0) return -1;

/* keep the faster sample so one slow syn does not dominate the ui */
    int best = -1;
    for (int n = 0; n < 2; n++) {
        int ms = probe_tcp_ipv4(d, ip, port, timeout_ms);
        if (ms < 0) continue;
        if (best < 0 || ms < best) best = ms;
    }
    return best;
}

/* dial host:port through local socks, pump loop until deadline */
static int socks_dial_retry(daemon_ctl_t *d, uint16_t sp,
                            const char *host, uint16_t port,
                            long deadline, const char **stage) {
    int fd = -1;
    while (probe_now_ms() < deadline) {
        subfetch_pump_loop(d);
        const char *dstage = NULL;
        fd = socks5_dial_via_loop(d, sp, host, port, deadline, &dstage);
        if (fd >= 0) return fd;
        if (dstage && stage) *stage = dstage;
        struct pollfd pfd = { -1, 0, 0 };
        poll(&pfd, 0, 50);
    }
    return -1;
}

static int read_some_pumped(int fd, char *buf, size_t cap, size_t want_min,
                            daemon_ctl_t *d, long deadline, size_t *got_out) {
    size_t got = 0;
    while (got < cap && probe_now_ms() < deadline) {
        subfetch_pump_loop(d);
        ssize_t r = read(fd, buf + got, cap - got);
        if (r > 0) {
            got += (size_t)r;
            if (got >= want_min) break;
            continue;
        }
        if (r == 0) break;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            struct pollfd pfd = { fd, POLLIN, 0 };
            poll(&pfd, 1, 50);
            continue;
        }
        break;
    }
    if (got_out) *got_out = got;
    return (got >= want_min) ? 0 : -1;
}

/* test reality with a tiny clienthello because any tls record proves transport */
static int build_probe_clienthello_record(uint8_t *out, size_t cap, size_t *out_len,
                                          const char *sni) {
    if (!out || !out_len || cap < 10) return -1;
    tls_ch_params_t chp;
    memset(&chp, 0, sizeof chp);
    if (RAND_bytes(chp.random, sizeof chp.random) != 1) return -1;
    if (RAND_bytes(chp.x25519_pub, sizeof chp.x25519_pub) != 1) return -1;
/* shape the decoy share enough to trigger a serverhello response */
    chp.sni = sni;
    chp.fp = TLS_FP_CHROME;

    uint8_t hello[2048];
    size_t hello_len = 0;
    if (tls_build_clienthello(&chp, hello, sizeof hello, &hello_len) != TLS_CH_OK)
        return -1;
    if (5 + hello_len > cap) return -1;
    out[0] = 0x16; /* handshake */
    out[1] = 0x03;
    out[2] = 0x01;
    out[3] = (uint8_t)(hello_len >> 8);
    out[4] = (uint8_t)(hello_len & 0xff);
    memcpy(out + 5, hello, hello_len);
    *out_len = 5 + hello_len;
    return 0;
}

static int is_tls_record_prefix(const char *buf, size_t n) {
    if (n < 5) return 0;
    uint8_t t = (uint8_t)buf[0];
/* handshake / alert / change_cipher_spec / app_data */
    if (t != 0x14 && t != 0x15 && t != 0x16 && t != 0x17) return 0;
    if ((uint8_t)buf[1] != 0x03) return 0;
    return 1;
}

/* probe tls and http payload paths so failure reasons reach the ui */
static int tunnel_carry_probe(daemon_ctl_t *d, int timeout_ms, int *ms_out,
                              char *stage_out, size_t stage_cap) {
    if (ms_out) *ms_out = -1;
    if (stage_out && stage_cap) stage_out[0] = '\0';
    long start = probe_now_ms();
    long deadline = start + timeout_ms;
    const char *stage = "tunnel verify failed";
    uint16_t sp;

    if (!d || !d->loop) {
        stage = "socks not ready";
        goto fail;
    }
    sp = loop_listen_port(d->loop);
    if (!sp) {
        stage = "socks not ready";
        goto fail;
    }

/* test tls first because vision nodes usually carry https */
    {
        long half = start + (timeout_ms * 6) / 10; /* 60% budget */
        if (half > deadline) half = deadline;
        int fd = socks_dial_retry(d, sp, "example.com", 443, half, &stage);
        if (fd >= 0) {
            uint8_t rec[2100];
            size_t rec_len = 0;
            if (build_probe_clienthello_record(rec, sizeof rec, &rec_len,
                                               "example.com") == 0 &&
                write_all_pumped_until(fd, rec, rec_len, d, half) == 0) {
                char buf[64];
                size_t got = 0;
                if (read_some_pumped(fd, buf, sizeof buf, 5, d, half, &got) == 0 &&
                    is_tls_record_prefix(buf, got)) {
                    close(fd);
                    if (ms_out) *ms_out = (int)(probe_now_ms() - start);
                    return 0;
                }
                stage = got ? "bad tls response" : "no tls response";
            } else {
                stage = "probe write failed";
            }
            close(fd);
        }
    }

/* test http second because some nodes reject plain port 80 */
    {
        int fd = socks_dial_retry(d, sp, "example.com", 80, deadline, &stage);
        if (fd < 0) goto fail;

        static const char req[] =
            "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
        if (write_all_pumped_until(fd, req, sizeof req - 1, d, deadline) != 0) {
            close(fd);
            stage = "probe write failed";
            goto fail;
        }

        char buf[256];
        size_t got = 0;
        if (read_some_pumped(fd, buf, sizeof buf - 1, 12, d, deadline, &got) != 0) {
            close(fd);
            stage = "no http response";
            goto fail;
        }
        close(fd);
        if (memcmp(buf, "HTTP/", 5) != 0) {
            stage = "bad http response";
            goto fail;
        }
        if (ms_out) *ms_out = (int)(probe_now_ms() - start);
        return 0;
    }

fail:
    if (stage_out && stage_cap && stage)
        snprintf(stage_out, stage_cap, "%s", stage);
    return -1;
}

int daemon_ctl_verify_tunnel(void *ctx, char *reason, size_t reason_cap) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
/* keep per-try short so 4 failovers stay under the ui connect budget */
    const int timeout_ms = 8000;
    char stage[120];
    stage[0] = '\0';
    int r = tunnel_carry_probe(d, timeout_ms, NULL, stage, sizeof stage);
/* retain a short failure stage so state error remains actionable */
    if (r != 0) {
        fprintf(stderr, "senkod: tunnel verify failed: %s (%dms)\n",
                stage[0] ? stage : "unknown", timeout_ms);
        if (reason && reason_cap)
            snprintf(reason, reason_cap, "%s",
                     stage[0] ? stage : "tunnel verify failed");
        vpn_icon_set(0);
    } else if (reason && reason_cap) {
        reason[0] = '\0';
        vpn_icon_set(1); /* only show vpn after a real carry probe */
    }
    return r;
}

int daemon_ctl_ping_tunnel(void *ctx) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    const int timeout_ms = 4000;
    int ms = -1;
    return (tunnel_carry_probe(d, timeout_ms, &ms, NULL, 0) == 0) ? ms : -1;
}
