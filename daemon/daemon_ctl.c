#define _DEFAULT_SOURCE /* expose getaddrinfo */

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
#include <sys/stat.h>
#include <sys/time.h>
#include <openssl/rand.h>

void daemon_ctl_init(daemon_ctl_t *d, loop_t *loop, const char *config_path) {
    if (!d) return;
    memset(d, 0, sizeof *d);
    d->go.tun_fd = -1;
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
    if (d->full_device) {
        go_backend_stop(&d->go);
        c_backend_stop(&d->c_backend, d->loop);
    }
}

int daemon_ctl_maintain(daemon_ctl_t *d) {
    if (!d || !d->go.active) return 0;
    if (go_backend_running(&d->go)) return 0;
    fprintf(stderr, "senkod: go backend core exited unexpectedly\n");
    go_backend_stop(&d->go);
    loop_stop(d->loop);
    vpn_icon_set(0);
    return -1;
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
    hints.ai_family = AF_INET; /* use ipv4 for routing */
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return -1;

    int ok = 0;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET) continue;
        if (reject_unsafe && !net_addr_allowed(ai->ai_addr)) {
            ok = -1;
            break;
        }
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

static int routing_path_probe(daemon_ctl_t *d, int timeout_ms);

/* the c backend ladder only learns whether pfctl or ipfw accepted a ruleset,
   so senkod has to push a real connection through and watch it arrive */
static int c_backend_verify(void *ctx) {
    return routing_path_probe((daemon_ctl_t *)ctx, 2000);
}

int daemon_ctl_apply(void *ctx, const ctl_action_t *action) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (!d || !d->loop || !action) return -1;

    switch (action->kind) {
        case CTL_ACT_START: {
            const vl_server_t *s = &action->server;

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

            if (d->full_device && (d->go.active || d->c_backend.active)) {
                go_backend_stop(&d->go);
                c_backend_stop(&d->c_backend, d->loop);
            }

            dialer_set_target(&d->dialer, s->host, s->port);

            if (loop_set_server(d->loop, vt, dialer_connect, &d->dialer,
                                s->proto, uuid, s->flow, s->user, s->pass,
                                s->sni, s->fp, s->pbk, s->sid, s->path,
                                s->ws_host, s->mode, s->host) != LOOP_OK) {
                fprintf(stderr, "senkod: socks listener failed\n");
                vpn_icon_set(0);
                return DCTL_ERR_LOOP;
            }

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

                /* the verification target only has to be a stable public
                   address, and resolving it again on every connect put a dns
                   round trip in front of every tunnel coming up */
                if (!d->probe_ip[0])
                    (void)resolve_ipv4_addresses("example.com", d->probe_ip,
                                                 sizeof d->probe_ip, NULL, 0, 1);

                char backend_reason[160];
                backend_reason[0] = '\0';
                d->last_reason[0] = '\0';
                int go_attempted = go_backend_supported();
                int routing_ok;
                if (go_attempted) {
                    routing_ok = go_backend_start(&d->go, s, first_ip,
                                                  backend_reason,
                                                  sizeof backend_reason) == 0;
                    if (!routing_ok)
                        fprintf(stderr, "senkod: go backend failed: %s\n",
                                backend_reason[0] ? backend_reason : "unknown error");
                } else {
                    routing_ok = c_backend_start(&d->c_backend, d->loop,
                                                 (int)loop_listen_port(d->loop),
                                                 first_ip, ip_list,
                                                 d->settings.dns_upstream,
                                                 (int)d->settings.dns_local_port,
                                                 c_backend_verify, d,
                                                 backend_reason,
                                                 sizeof backend_reason) == 0;
                    if (!routing_ok)
                        fprintf(stderr, "senkod: c backend failed: %s\n",
                                backend_reason[0] ? backend_reason : "unknown error");
                }
                if (!routing_ok) {
                    snprintf(d->last_reason, sizeof d->last_reason, "%s",
                             backend_reason);
                    loop_stop(d->loop);
                    vpn_icon_set(0);
                    return go_attempted ? DCTL_ERR_GO : DCTL_ERR_ROUTING;
                }
                if (c_backend_uses_tproxy(&d->c_backend))
                    fprintf(stderr, "senkod: c backend: transparent tcp on port %d\n",
                            d->c_backend.redir_port);
            }
            return 0;
        }

        case CTL_ACT_STOP:
            vpn_icon_set(0);
            if (d->full_device) {
                go_backend_stop(&d->go);
                c_backend_stop(&d->c_backend, d->loop);
            }
            loop_stop(d->loop);
            return 0;

        case CTL_ACT_PING:
            return 0;

        case CTL_ACT_REFRESH:
            return -1;

        case CTL_ACT_NONE:
        default:
            return 0;
    }
}

const char *daemon_ctl_last_reason(void *ctx) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    return d && d->last_reason[0] ? d->last_reason : NULL;
}

void daemon_ctl_persist(void *ctx, const store_t *store) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (!d || !store || !d->config_path[0]) return;
    storefile_save(store, &d->settings, d->config_path);
}

#define SENKO_BACKUP_EXPORT "/var/mobile/Documents/senko-backup.senko"
#define SENKO_BACKUP_IMPORT "/var/mobile/Library/Preferences/Senko/import.senko"

int daemon_ctl_backup(void *ctx, int restore, store_t *store) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (!d || !store || !d->config_path[0]) return -1;
    if (!restore) {
        if (storefile_save(store, &d->settings, SENKO_BACKUP_EXPORT) != STOREFILE_OK)
            return -1;
        (void)chown(SENKO_BACKUP_EXPORT, 501, 501);
        return 0;
    }
    struct stat staged;
    if (lstat(SENKO_BACKUP_IMPORT, &staged) != 0 || !S_ISREG(staged.st_mode))
        return -1;
    /* a store is half a megabyte, which the ios 5 stack cannot carry. control
       commands are handled one at a time, so one static staging copy is enough */
    static store_t candidate;
    daemon_settings_t candidate_settings;
    if (storefile_load(&candidate, &candidate_settings, SENKO_BACKUP_IMPORT) != STOREFILE_OK)
        return -1;
    if (storefile_save(&candidate, &candidate_settings, d->config_path) != STOREFILE_OK)
        return -1;
    *store = candidate;
    d->settings = candidate_settings;
    (void)unlink(SENKO_BACKUP_IMPORT);
    return 0;
}

static void subfetch_pump_loop(void *ctx) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (d && d->loop) loop_step(d->loop, 0);
}

/* wall clock changes must not extend network deadlines */
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

static int socks5_dial_via_loop(daemon_ctl_t *d, uint16_t socks_port,
                                  const char *host, uint16_t port,
                                  long deadline_ms, const char **stage_out) {
    if (stage_out) *stage_out = "socks dial failed";
    size_t hlen = strlen(host);
    if (hlen > 255) return -1;
    struct in_addr dst4;
    struct in6_addr dst6;
    int dst_family = inet_pton(AF_INET, host, &dst4) == 1 ? AF_INET :
                     (inet_pton(AF_INET6, host, &dst6) == 1 ? AF_INET6 : 0);
    if (!dst_family && !net_hostname_safe(host)) return -1;

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
    if (dst_family == AF_INET) {
        req[n++] = 0x01;
        memcpy(req + n, &dst4, 4);
        n += 4;
    } else if (dst_family == AF_INET6) {
        req[n++] = 0x04;
        memcpy(req + n, &dst6, 16);
        n += 16;
    } else {
        req[n++] = 0x03;
        req[n++] = (uint8_t)hlen;
        memcpy(req + n, host, hlen);
        n += hlen;
    }
    req[n++] = (uint8_t)(port >> 8);
    req[n++] = (uint8_t)(port & 0xff);
    if (write_all_pumped_until(fd, req, n, d, deadline_ms) != 0) {
        if (stage_out) *stage_out = "socks request write failed";
        close(fd);
        return -1;
    }

    uint8_t rhdr[4];
    if (read_full_pumped_until(fd, rhdr, 4, d, deadline_ms) != 0) {
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

static int subfetch_dial_direct(const char *host, uint16_t port) {
    char portstr[8];
    snprintf(portstr, sizeof portstr, "%u", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        if (!net_addr_allowed(ai->ai_addr)) continue;
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            int fl = fcntl(fd, F_GETFL, 0); /* keep fetch nonblocking */
            fcntl(fd, F_SETFL, fl | O_NONBLOCK);
            break;
        }
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int subfetch_dial(void *ctx, const char *host, uint16_t port) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (d && d->full_device && d->c_backend.active && d->loop) {
        uint16_t sp = loop_listen_port(d->loop);
        if (sp) {
            long deadline = probe_now_ms() + 8000;
            int fd = socks5_dial_via_loop(d, sp, host, port, deadline, NULL);
            if (fd >= 0) return fd;
        }
    }
    return subfetch_dial_direct(host, port);
}

int daemon_ctl_fetch(void *ctx, const char *url,
                     const char *request_header,
                     unsigned char *buf, size_t cap, size_t *len,
                     ctl_fetch_meta_t *meta) {
    subfetch_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.dial = subfetch_dial;
    cfg.dial_ctx = ctx;
    cfg.pump = subfetch_pump_loop;
    cfg.pump_ctx = ctx;
    cfg.tcp = &transport_tcp;
    cfg.tls = &transport_tls; /* use tls for https */
    cfg.request_header = request_header;
    cfg.max_redirects = 5;

    subfetch_info_t info;
    subfetch_status_t r = subfetch_get_info(&cfg, url, buf, cap, len, 15000, &info);
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
/* redaction prevents subscription credentials from reaching system logs */
        url_t u;
        if (url && url_parse(url, &u) == URL_OK)
            fprintf(stderr, "senkod: subfetch failed: %s (rc=%d) %s://%s:%u/...\n",
                    why, (int)r, u.is_https ? "https" : "http", u.host, (unsigned)u.port);
        else
            fprintf(stderr, "senkod: subfetch failed: %s (rc=%d)\n", why, (int)r);
        return -1;
    }
    if (meta) {
        meta->expire = info.expire;
        meta->upload = info.upload;
        meta->download = info.download;
        meta->total = info.total;
        snprintf(meta->description, sizeof meta->description, "%s", info.description);
        snprintf(meta->support_url, sizeof meta->support_url, "%s", info.support_url);
        meta->gated = info.gated;
        snprintf(meta->gate_reason, sizeof meta->gate_reason, "%s", info.gate_reason);
    }
    fprintf(stderr, "senkod: subfetch ok %zu bytes\n", len ? *len : 0);
    return 0;
}

static int dial_numeric_until(daemon_ctl_t *d, const char *ip, uint16_t port,
                              long deadline, int *elapsed_ms) {
    struct sockaddr_storage storage;
    struct sockaddr *addr = (struct sockaddr *)&storage;
    socklen_t addr_len;
    memset(&storage, 0, sizeof storage);
    if (inet_pton(AF_INET, ip, &((struct sockaddr_in *)addr)->sin_addr) == 1) {
        struct sockaddr_in *v4 = (struct sockaddr_in *)addr;
        v4->sin_family = AF_INET;
        v4->sin_port = htons(port);
        addr_len = sizeof *v4;
    } else if (inet_pton(AF_INET6, ip, &((struct sockaddr_in6 *)addr)->sin6_addr) == 1) {
        struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)addr;
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(port);
        addr_len = sizeof *v6;
    } else {
        return -1;
    }
    int fd = socket(addr->sa_family, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    long start = probe_now_ms();
    int r = connect(fd, addr, addr_len);
    if (r == 0) {
        if (elapsed_ms) *elapsed_ms = (int)(probe_now_ms() - start);
        return fd;
    }
    if (errno != EINPROGRESS) { close(fd); return -1; }

    for (;;) {
        if (d) subfetch_pump_loop(d);
        long now = probe_now_ms();
        if (now >= deadline) { close(fd); return -1; }

        int remain = (int)(deadline - now);
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
        if (elapsed_ms) *elapsed_ms = (int)(probe_now_ms() - start);
        return fd;
    }
}

static int probe_tcp_numeric(daemon_ctl_t *d, const char *ip, uint16_t port,
                             int timeout_ms) {
    int ms = -1;
    int fd = dial_numeric_until(d, ip, port, probe_now_ms() + timeout_ms, &ms);
    if (fd >= 0) close(fd);
    return fd >= 0 ? ms : -1;
}

int daemon_ctl_probe(void *ctx, const char *host, uint16_t port) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    const int timeout_ms = 1500; /* keep two tries under the client timeout */

    char ip[INET6_ADDRSTRLEN];
    if (!net_resolve_public(host, port, ip, sizeof ip)) return -1;

    /* without a bypass the catch-all redirect sends the probe through the
       tunnel, so the reported latency belongs to the tunnel, not the server */
    if (d && d->full_device) c_backend_bypass_add_ipv4(&d->c_backend, ip);

    int best = -1;
    for (int n = 0; n < 2; n++) {
        int ms = probe_tcp_numeric(d, ip, port, timeout_ms);
        if (ms < 0) continue;
        if (best < 0 || ms < best) best = ms;
    }
    return best;
}

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

static int build_probe_clienthello_record(uint8_t *out, size_t cap, size_t *out_len,
                                          const char *sni) {
    if (!out || !out_len || cap < 10) return -1;
    tls_ch_params_t chp;
    memset(&chp, 0, sizeof chp);
    if (RAND_bytes(chp.random, sizeof chp.random) != 1) return -1;
    if (RAND_bytes(chp.x25519_pub, sizeof chp.x25519_pub) != 1) return -1;
    chp.sni = sni;
    chp.fp = TLS_FP_CHROME;

    uint8_t hello[2048];
    size_t hello_len = 0;
    if (tls_build_clienthello(&chp, hello, sizeof hello, &hello_len) != TLS_CH_OK)
        return -1;
    if (5 + hello_len > cap) return -1;
    out[0] = 0x16; /* tls handshake */
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
    if (t != 0x14 && t != 0x15 && t != 0x16 && t != 0x17) return 0;
    if ((uint8_t)buf[1] != 0x03) return 0;
    return 1;
}

static int tunnel_carry_probe(daemon_ctl_t *d, int timeout_ms, int *ms_out,
                              char *stage_out, size_t stage_cap) {
    if (ms_out) *ms_out = -1;
    if (stage_out && stage_cap) stage_out[0] = '\0';
    long start = probe_now_ms();
    long deadline = start + timeout_ms;
    const char *stage = "tunnel verify failed";
    uint16_t sp;
    char probe_ip[INET6_ADDRSTRLEN];

    if (!d || !d->loop) {
        stage = "socks not ready";
        goto fail;
    }
    sp = loop_listen_port(d->loop);
    if (!sp) {
        stage = "socks not ready";
        goto fail;
    }
    if (!net_resolve_public("example.com", 443, probe_ip, sizeof probe_ip)) {
        stage = "probe dns rejected";
        goto fail;
    }

    {
        long half = start + (timeout_ms * 6) / 10; /* reserve time for http */
        if (half > deadline) half = deadline;
        int fd = socks_dial_retry(d, sp, probe_ip, 443, half, &stage);
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

    {
        if (!net_resolve_public("example.com", 80, probe_ip, sizeof probe_ip)) {
            stage = "probe dns rejected";
            goto fail;
        }
        int fd = socks_dial_retry(d, sp, probe_ip, 80, deadline, &stage);
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

/* require application data through utun, a synthetic TUN connect is not success */
static int go_carry_probe(daemon_ctl_t *d, int timeout_ms, int *ms_out,
                          char *stage_out, size_t stage_cap) {
    char probe_ip[INET_ADDRSTRLEN];
    long start = probe_now_ms();
    long deadline = start + timeout_ms;
    const char *stage = "go backend core is not running";
    if (ms_out) *ms_out = -1;
    if (stage_out && stage_cap) stage_out[0] = '\0';
    if (!d || !go_backend_running(&d->go)) goto fail;
    if (resolve_ipv4_addresses("example.com", probe_ip, sizeof probe_ip,
                               NULL, 0, 1) != 0) {
        stage = "probe DNS failed";
        goto fail;
    }
    int fd = dial_numeric_until(d, probe_ip, 80, deadline, NULL);
    if (fd < 0) {
        stage = "TUN connection timed out";
        goto fail;
    }
    static const char request[] =
        "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    if (write_all_pumped_until(fd, request, sizeof request - 1, d, deadline) != 0) {
        close(fd);
        stage = "TUN data write failed";
        goto fail;
    }
    char response[64];
    size_t got = 0;
    if (read_some_pumped(fd, response, sizeof response, 12, d, deadline, &got) != 0) {
        close(fd);
        stage = "no data returned through TUN";
        goto fail;
    }
    close(fd);
    if (got < 5 || memcmp(response, "HTTP/", 5) != 0) {
        stage = "invalid data returned through TUN";
        goto fail;
    }
    if (ms_out) *ms_out = (int)(probe_now_ms() - start);
    return 0;

fail:
    if (stage_out && stage_cap) snprintf(stage_out, stage_cap, "%s", stage);
    return -1;
}

/* prove that an accepted firewall rule actually reaches the transparent listener */
static int routing_path_probe(daemon_ctl_t *d, int timeout_ms) {
    if (!d || !d->loop || !d->full_device) return 0;

    if (!d->probe_ip[0] &&
        resolve_ipv4_addresses("example.com", d->probe_ip, sizeof d->probe_ip,
                               NULL, 0, 1) != 0)
        return -1;
    const char *probe_ip = d->probe_ip;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        close(fd);
        return -1;
    }

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof dst);
    dst.sin_family = AF_INET;
    dst.sin_port = htons(80);
    if (inet_pton(AF_INET, probe_ip, &dst.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    uint64_t generation = loop_tproxy_generation(d->loop);
    int cr;
    do {
        cr = connect(fd, (struct sockaddr *)&dst, sizeof dst);
    } while (cr != 0 && errno == EINTR);
    if (cr != 0 && errno != EINPROGRESS && errno != EALREADY &&
        errno != EWOULDBLOCK) {
        close(fd);
        return -1;
    }

    long deadline = probe_now_ms() + timeout_ms;
    int redirected = 0;
    while (probe_now_ms() < deadline) {
        if (loop_step(d->loop, 20) != LOOP_OK) break;
        if (loop_tproxy_seen(d->loop, generation, probe_ip, 80)) {
            redirected = 1;
            break;
        }
    }
    close(fd);
    return redirected ? 0 : -1;
}

/* the connect hook has no listener to watch, and the go core owns its own
   utun, so only the transparent rulesets can be proven with a live connection */
static int backend_path_probe(daemon_ctl_t *d) {
    if (!d || !d->full_device) return 0;
    if (d->go.active) return go_backend_running(&d->go) ? 0 : -1;
    if (!d->c_backend.active) return -1;
    if (!c_backend_uses_tproxy(&d->c_backend)) return 0;
    return routing_path_probe(d, 2000);
}

int daemon_ctl_verify_tunnel(void *ctx, char *reason, size_t reason_cap) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (backend_path_probe(d) != 0) {
        static const char failure[] =
            "routing rules accepted but traffic was not redirected";
        fprintf(stderr, "senkod: routing verification failed: %s\n", failure);
        if (reason && reason_cap) snprintf(reason, reason_cap, "%s", failure);
        vpn_icon_set(0);
        return -1;
    }
    const int timeout_ms = 8000;
    char stage[120];
    stage[0] = '\0';
    int r = d && d->go.active
        ? go_carry_probe(d, timeout_ms, NULL, stage, sizeof stage)
        : tunnel_carry_probe(d, timeout_ms, NULL, stage, sizeof stage);
    if (r != 0) {
        fprintf(stderr, "senkod: tunnel verify failed: %s (%dms)\n",
                stage[0] ? stage : "unknown", timeout_ms);
        if (reason && reason_cap)
            snprintf(reason, reason_cap, "%s",
                     stage[0] ? stage : "tunnel verify failed");
        vpn_icon_set(0);
    } else if (reason && reason_cap) {
        reason[0] = '\0';
        vpn_icon_set(1); /* show vpn after a real probe */
    }
    return r;
}

int daemon_ctl_ping_tunnel(void *ctx) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    const int timeout_ms = 4000;
    int ms = -1;
    int rc = d && d->go.active
        ? go_carry_probe(d, timeout_ms, &ms, NULL, 0)
        : tunnel_carry_probe(d, timeout_ms, &ms, NULL, 0);
    return rc == 0 ? ms : -1;
}

static int prepare_server_probe(daemon_ctl_t *d, const vl_server_t *server,
                                char *reason, size_t reason_cap) {
    const transport_vt_t *vt = transport_for_server(server);
    if (!vt) {
        if (reason && reason_cap)
            snprintf(reason, reason_cap, "profile uses an unsupported transport or security mode");
        return -1;
    }
    char public_ip[INET6_ADDRSTRLEN];
    if (!net_resolve_public(server->host, server->port,
                            public_ip, sizeof public_ip)) {
        if (reason && reason_cap)
            snprintf(reason, reason_cap, "server address is unsafe or cannot be resolved");
        return -1;
    }

    uint8_t uuid[VLESS_UUID_LEN];
    memset(uuid, 0, sizeof uuid);
    if (server->proto == VL_PROTO_VLESS &&
        vless_uuid_parse(server->uuid, uuid) != VLESS_OK) {
        if (reason && reason_cap)
            snprintf(reason, reason_cap, "profile has an invalid UUID");
        return -1;
    }
    dialer_set_target(&d->dialer, server->host, server->port);
    if (loop_set_server(d->loop, vt, dialer_connect, &d->dialer,
                        server->proto, uuid, server->flow,
                        server->user, server->pass, server->sni, server->fp,
                        server->pbk, server->sid, server->path,
                        server->ws_host, server->mode,
                        server->host) != LOOP_OK) {
        if (reason && reason_cap)
            snprintf(reason, reason_cap, "local test proxy could not be prepared");
        return -1;
    }
    return 0;
}

int daemon_ctl_check(void *ctx, const char *mode, const vl_server_t *server,
                     char *reason, size_t reason_cap) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (!d || !mode || !server) return -1;
    if (strcmp(mode, "tcp") == 0)
        return daemon_ctl_probe(d, server->host, server->port);
    if (!d->loop || !loop_listen_port(d->loop)) {
        if (reason && reason_cap) snprintf(reason, reason_cap, "local proxy is not active");
        return -1;
    }
    if (strcmp(mode, "proxy") == 0) {
        char numeric[INET6_ADDRSTRLEN];
        if (!net_resolve_public(server->host, server->port, numeric, sizeof numeric)) {
            if (reason && reason_cap) snprintf(reason, reason_cap, "unsafe or unresolved address");
            return -1;
        }
        long start = probe_now_ms();
        int fd = socks5_dial_via_loop(d, loop_listen_port(d->loop), numeric,
                                      server->port, start + 5000, NULL);
        if (fd < 0) {
            if (reason && reason_cap) snprintf(reason, reason_cap, "local proxy check failed");
            return -1;
        }
        close(fd);
        return (int)(probe_now_ms() - start);
    }
    if (strcmp(mode, "tunnel") == 0) {
        int ms = daemon_ctl_ping_tunnel(d);
        if (ms < 0 && reason && reason_cap) snprintf(reason, reason_cap, "active tunnel check failed");
        return ms;
    }
    if (strcmp(mode, "handshake") == 0) {
        if (d->loop->active) {
            if (reason && reason_cap)
                snprintf(reason, reason_cap, "disconnect before checking another profile");
            return -1;
        }
        if (prepare_server_probe(d, server, reason, reason_cap) != 0)
            return -1;
        int ms = -1;
        char stage[120];
        stage[0] = '\0';
        int result = tunnel_carry_probe(d, 7000, &ms, stage, sizeof stage);
        loop_stop(d->loop);
        if (result != 0) {
            if (reason && reason_cap)
                snprintf(reason, reason_cap, "profile handshake failed: %s",
                         stage[0] ? stage : "no valid response");
            return -1;
        }
        return ms;
    }
    if (reason && reason_cap) snprintf(reason, reason_cap, "unknown check type");
    return -1;
}
