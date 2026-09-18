#define _DEFAULT_SOURCE /* expose getaddrinfo */

#include "daemon_ctl.h"
#include "storefile.h"
#include "status.h"

#include "core/transport.h"
#include "core/transport_pick.h"
#include "core/vless.h"
#include "core/subfetch.h"
#include "core/net_safe.h"
#include "core/url.h"
#include "core/tls_clienthello.h"
#include "core/control.h"
#include "core/senko_trace.h"
#include "core/dns_cache.h"
#include "core/blake2b256.h"
#include "legacy_ios.h"
#include "go_config.h"
#include "routing.h"
#include "../common/senko_paths.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#include <net/if.h>
#endif
#include <openssl/rand.h>

void daemon_ctl_init(daemon_ctl_t *d, loop_t *loop, const char *config_path) {
    if (!d) return;
    memset(d, 0, sizeof *d);
    pthread_mutex_init(&d->probe_route_lock, NULL);
    d->go.tun_fd = -1;
    d->loop = loop;
    d->started_at = ctl_engine_now();
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
    senko_trace_set_enabled(d->settings.trace);
}

void daemon_ctl_set_rules(daemon_ctl_t *d, ruleset_t *rules) {
    if (d) d->rules = rules;
}

void daemon_ctl_shutdown(daemon_ctl_t *d) {
    if (!d) return;
    status_set(0);
    if (d->full_device) {
        go_backend_stop(&d->go);
        c_backend_stop(&d->c_backend, d->loop);
    }
    pthread_mutex_destroy(&d->probe_route_lock);
}

int daemon_ctl_maintain(daemon_ctl_t *d) {
    if (!d || !d->go.active) return 0;
    if (go_backend_running(&d->go)) return 0;
    fprintf(stderr, "senkod: go backend core exited unexpectedly\n");
    go_backend_stop(&d->go);
    loop_stop(d->loop);
    status_set(0);
    return -1;
}

int daemon_ctl_stats(void *ctx, uint64_t *up, uint64_t *down) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (up) *up = 0;
    if (down) *down = 0;
    if (!d || !d->loop || !up || !down) return -1;
    if (d->go.active) return go_backend_stats(&d->go, up, down);
    *up = d->loop->bytes_up;
    *down = d->loop->bytes_down;
    return 0;
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
    if (net_getaddrinfo_timed(host, NULL, &hints, &res, 2000) != 0 || !res) return -1;

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

int daemon_ctl_native_config(void *ctx, const vl_server_t *server, char *buf,
                             size_t cap, size_t *len) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    char endpoint[INET_ADDRSTRLEN];
    if (len) *len = 0;
    if (!d || !server || !buf || cap < 2 || !len)
        return -1;
    endpoint[0] = '\0';
    if (resolve_ipv4_addresses(server->host, endpoint, sizeof endpoint,
                               NULL, 0, 0) != 0)
        return -1;
    if (go_config_render_rules(server, endpoint, "utun", d->rules,
                               buf, cap) != 0)
        return -1;
    *len = strlen(buf);
    return *len > 0 ? 0 : -1;
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
/* quic/udp only: no senko transport carries it, so there is nothing to hand
   the local socks loop. the go core dials it directly with its own bundled
   hysteria client, which means this profile needs the go backend and the
   full-device tunnel that owns it */
            int quic_only = (s->proto == VL_PROTO_HYSTERIA2);
            if (quic_only) {
                if (!d->full_device) {
                    fprintf(stderr, "senkod: hysteria2 requires full-device mode\n");
                    status_set(0);
                    return DCTL_ERR_TRANSPORT;
                }
                if (d->settings.force_backend == SENKO_BACKEND_C ||
                    d->settings.force_backend == SENKO_BACKEND_APP_PROXY) {
                    fprintf(stderr, "senkod: hysteria2 requires the go backend, "
                                    "but the c backend is pinned in settings\n");
                    status_set(0);
                    return DCTL_ERR_TRANSPORT;
                }
                if (!go_backend_supported()) {
                    fprintf(stderr, "senkod: hysteria2 requires the go backend, "
                                    "unsupported on this device\n");
                    status_set(0);
                    return DCTL_ERR_TRANSPORT;
                }
            }

            const transport_vt_t *vt = quic_only ? NULL : transport_for_server(s);
            if (!vt && !quic_only) {
                fprintf(stderr, "senkod: unsupported transport/security for server\n");
                status_set(0);
                return DCTL_ERR_TRANSPORT;
            }

            uint8_t uuid[VLESS_UUID_LEN];
            memset(uuid, 0, sizeof uuid);
            if (s->proto == VL_PROTO_VLESS) {
                if (vless_uuid_parse(s->uuid, uuid) != VLESS_OK) {
                    fprintf(stderr, "senkod: bad uuid in server link\n");
                    status_set(0);
                    return DCTL_ERR_UUID;
                }
            }

            if (d->full_device && (d->go.active || d->c_backend.active)) {
                go_backend_stop(&d->go);
                c_backend_stop(&d->c_backend, d->loop);
            }

            if (quic_only) {
/* nothing local backs this profile; drop any listener left from the last one */
                loop_stop(d->loop);
            } else {
                dialer_set_target(&d->dialer, s->host, s->port);

                if (loop_set_server(d->loop, vt, dialer_connect, &d->dialer,
                                    s->proto, uuid, s->flow, s->user, s->pass,
                                    s->sni, s->fp, s->pbk, s->sid, s->path,
                                    s->ws_host, s->mode, s->host, s->insecure) != LOOP_OK) {
                    fprintf(stderr, "senkod: socks listener failed\n");
                    status_set(0);
                    return DCTL_ERR_LOOP;
                }
            }

            if (d->full_device) {
                char first_ip[64], ip_list[4096];
                first_ip[0] = '\0';
                ip_list[0] = '\0';

                if (resolve_ipv4_addresses(s->host, first_ip, sizeof first_ip,
                                           ip_list, sizeof ip_list, 0) != 0) {
                    fprintf(stderr, "senkod: dns resolution failed for %s\n", s->host);
                    loop_stop(d->loop);
                    status_set(0);
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
                senko_force_t force;
                force.backend = d->settings.force_backend;
                force.pf_mode = d->settings.force_pf_mode;
/* a pin is respected even when it cannot work: go_backend_start names the
   reason it will not run on this device, which is the answer the tester came
   for, and is more use than quietly taking the c core instead */
                int go_attempted = quic_only || force.backend == SENKO_BACKEND_GO ||
                    (force.backend == SENKO_BACKEND_AUTO && go_backend_supported());
                int routing_ok;
                if (go_attempted) {
                    routing_ok = go_backend_start(&d->go, s, first_ip, d->rules,
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
                                                 d->settings.block_response,
                                                 d->rules, &force,
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
                    status_set(0);
                    return go_attempted ? DCTL_ERR_GO : DCTL_ERR_ROUTING;
                }
                if (c_backend_uses_tproxy(&d->c_backend))
                    fprintf(stderr, "senkod: c backend: transparent tcp on port %d\n",
                            d->c_backend.redir_port);
            }
            return 0;
        }

        case CTL_ACT_STOP:
            status_set(0);
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

        case CTL_ACT_SET: {
/* the daemon holds the only writable copy, so the control server hands the
   pair over instead of keeping settings the two could disagree about */
            settings_status_t r = daemon_settings_set(&d->settings,
                                                      action->key,
                                                      strlen(action->key),
                                                      action->value,
                                                      strlen(action->value));
            if (r == SETTINGS_ERR_KEY) return DCTL_ERR_SETTING_KEY;
            if (r != SETTINGS_OK) return DCTL_ERR_SETTING_VALUE;
            senko_trace_set_enabled(d->settings.trace);
            return 0;
        }

        case CTL_ACT_NONE:
        default:
            return 0;
    }
}

const char *daemon_ctl_last_reason(void *ctx) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    return d && d->last_reason[0] ? d->last_reason : NULL;
}

/* every fact is appended through one helper so a full buffer stops the report
   instead of writing a half line the reader would show as a fact */
static void diag_add(char *buf, size_t cap, size_t *off, const char *key,
                     const char *fmt, ...) {
    char value[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(value, sizeof value, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    size_t written = 0;
    if (ctl_build_diag(key, value, buf + *off, cap - *off, &written) == CTL_OK)
        *off += written;
}

static const char *proc_state(const char *pid_path, char *out, size_t cap) {
    FILE *f = fopen(pid_path, "r");
    long pid = 0;
    int ok = f && fscanf(f, "%ld", &pid) == 1 && pid > 1 && kill((pid_t)pid, 0) == 0;
    if (f) fclose(f);
    if (!ok) {
        snprintf(out, cap, "not running");
        return out;
    }
    snprintf(out, cap, "pid %ld", pid);
    return out;
}

static const char *file_state(const char *path, char *out, size_t cap) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        snprintf(out, cap, "missing");
        return out;
    }
/* the package installs its payload under /var/jb and links to it from the
   system path, so the link's own length is not the size of what substrate
   will map. reporting 33 bytes for a dylib is worse than reporting nothing */
    if (S_ISLNK(st.st_mode)) {
        struct stat target;
        if (stat(path, &target) != 0) {
            snprintf(out, cap, "dangling link");
            return out;
        }
        snprintf(out, cap, "installed (%lld bytes, through a link)",
                 (long long)target.st_size);
        return out;
    }
    snprintf(out, cap, "installed (%lld bytes)", (long long)st.st_size);
    return out;
}

/* senko-kick is a one shot helper, not a resident process, so the useful facts
   are whether it can run at all and when it last did */
static const char *kick_state(char *out, size_t cap) {
    struct stat binary;
    struct stat log;
    if (stat(SENKO_USR_BIN "/senko-kick", &binary) != 0) {
        snprintf(out, cap, "missing");
        return out;
    }
    if (access(SENKO_USR_BIN "/senko-kick", X_OK) != 0) {
        snprintf(out, cap, "installed, not executable");
        return out;
    }
    if (stat("/var/log/senko-kick.log", &log) == 0) {
        long age = (long)(time(NULL) - log.st_mtime);
        if (age < 0) age = 0;
        snprintf(out, cap, "ready, last ran %ld s ago", age);
        return out;
    }
    snprintf(out, cap, "ready, never ran");
    return out;
}

/* the rules that are doing something. a rule list nobody can see hit counts for
   is a list of guesses */
static void diag_top_rules(const ruleset_t *rules, char *buf, size_t cap,
                           size_t *off) {
    size_t best[3];
    size_t found = 0;
    if (!rules || rules->count == 0) return;
    for (size_t round = 0; round < 3; ++round) {
        size_t pick = rules->count;
        uint64_t best_hits = 0;
        for (size_t i = 0; i < rules->count; ++i) {
            uint64_t hits = rule_hit_count(&rules->entries[i]);
            if (hits == 0) continue;
            int taken = 0;
            for (size_t j = 0; j < found; ++j) if (best[j] == i) taken = 1;
            if (taken) continue;
            if (pick == rules->count || hits > best_hits) {
                pick = i;
                best_hits = hits;
            }
        }
        if (pick == rules->count) break;
        best[found++] = pick;
    }
    if (found == 0) {
        diag_add(buf, cap, off, "rules.top", "no rule has matched yet");
        return;
    }
    for (size_t i = 0; i < found; ++i) {
        const rule_t *rule = &rules->entries[best[i]];
        char key[24];
        snprintf(key, sizeof key, "rules.top%zu", i + 1);
        diag_add(buf, cap, off, key, "%llu hit(s), %s %s %s",
                 (unsigned long long)rule_hit_count(rule),
                 rule_action_name(rule->action), rule_type_name(rule->type),
                 rule->value);
    }
}

int daemon_ctl_diag(void *ctx, char *buf, size_t cap, size_t *len) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    size_t off = 0;
    char scratch[192];
    if (!d || !buf || !len || cap == 0) return -1;
    *len = 0;

    diag_add(buf, cap, &off, "daemon.pid", "%ld", (long)getpid());
    diag_add(buf, cap, &off, "daemon.uptime", "%ld s",
             (long)(ctl_engine_now() - d->started_at));
#if defined(SENKO_ROOTLESS)
    diag_add(buf, cap, &off, "daemon.jailbreak", "rootless, root at %s", SENKO_JBROOT);
#else
    diag_add(buf, cap, &off, "daemon.jailbreak", "rootful, root at /");
#endif
    diag_add(buf, cap, &off, "daemon.config", "%s",
             d->config_path[0] ? d->config_path : "none");
    diag_add(buf, cap, &off, "ios.major", "%d", senko_ios_major());
    diag_add(buf, cap, &off, "ios.source", "%s", senko_ios_major_source());

/* the backend ladder: which one carries traffic right now, and why the one
   above it was not eligible */
    if (!d->full_device)
        diag_add(buf, cap, &off, "backend", "socks only (full-device off)");
    else if (d->go.active)
        diag_add(buf, cap, &off, "backend", "go core on %s", d->go.route.ifname);
    else if (d->c_backend.app_proxy)
        diag_add(buf, cap, &off, "backend", "c core, connect hook");
    else if (d->c_backend.active)
        diag_add(buf, cap, &off, "backend", "c core, transparent listener");
    else
        diag_add(buf, cap, &off, "backend", "idle");
    diag_add(buf, cap, &off, "backend.go_supported", "%s",
             go_backend_supported() ? "yes" : sizeof(void *) == 8
                 ? "no, ios below 12" : "no, 32 bit slice");
    if (d->settings.force_backend != SENKO_BACKEND_AUTO)
        diag_add(buf, cap, &off, "backend.pinned", "%s",
                 daemon_settings_backend_name(d->settings.force_backend));
/* the pin is reported whether or not a backend is up: it is set while nothing
   is connected, and a pin nobody can confirm is a pin nobody trusts */
    if (d->settings.force_pf_mode != SENKO_PF_MODE_AUTO)
        diag_add(buf, cap, &off, "firewall.pf_pinned", "%s",
                 routing_pf_mode_name((routing_pf_mode_t)d->settings.force_pf_mode));

    const routing_exec_t *rx = &d->c_backend.rules;
    if (d->c_backend.active) {
        diag_add(buf, cap, &off, "firewall", "%s",
                 rx->mode == ROUTING_MODE_PF ? "pf"
                 : rx->mode == ROUTING_MODE_IPFW ? "ipfw" : "none");
        if (rx->pf_mode_valid)
            diag_add(buf, cap, &off, "firewall.pf_mode", "%s, %d variant(s) refused first",
                     routing_pf_mode_name(rx->pf_mode), rx->pf_rejected);
        if (rx->pf_last_error[0])
            diag_add(buf, cap, &off, "firewall.last_reject", "%s", rx->pf_last_error);
        {
            pf_table_counts_t counts;
            uint64_t evicted_addresses = 0;
            uint64_t evicted_refs = 0;
            routing_exec_bypass_stats(&counts, &evicted_addresses, &evicted_refs);
            diag_add(buf, cap, &off, "firewall.bypass_table",
                     "%s, %zu address(es), %zu in pf, %zu name ref(s)",
                     rx->pf_table_ready ? "live" : "static only",
                     counts.addresses, counts.installed, counts.refs);
            diag_add(buf, cap, &off, "firewall.bypass_evicted",
                     "%llu address(es), %llu name ref(s) dropped by capacity",
                     (unsigned long long)evicted_addresses,
                     (unsigned long long)evicted_refs);
        }
/* the configured ports and the ones in force are routinely different, because
   both are picked from a free range when the configured one is busy */
        diag_add(buf, cap, &off, "port.redirect", "%d in force", rx->redir_port);
        diag_add(buf, cap, &off, "port.dns", "%d in force, %u configured",
                 rx->dns_local_port, (unsigned)d->settings.dns_local_port);
    }
    diag_add(buf, cap, &off, "port.socks", "%u in force, %u configured",
             (unsigned)(d->loop ? loop_listen_port(d->loop) : 0),
             (unsigned)d->settings.socks_port);
    diag_add(buf, cap, &off, "socks.bind", "%s",
             d->settings.socks_public ? "0.0.0.0, reachable from the network"
                                      : "127.0.0.1");
    diag_add(buf, cap, &off, "conns", "%zu live of %d",
             d->loop ? loop_conn_count(d->loop) : (size_t)0, LOOP_MAX_CONNS);
    diag_add(buf, cap, &off, "dns.upstream", "%s", d->settings.dns_upstream);
    diag_add(buf, cap, &off, "dns.block_response", "%s",
             d->settings.block_response == DNS_BLOCK_NXDOMAIN ? "nxdomain" :
             d->settings.block_response == DNS_BLOCK_REFUSED ? "refused" : "zero");
    {
        uint64_t hits = 0, misses = 0, stale = 0;
        size_t entries = 0;
        routing_exec_dns_stats(&hits, &misses, &stale, &entries);
        diag_add(buf, cap, &off, "dns.cache",
                 "%llu hit, %llu miss, %llu stale, %zu of %d entries",
                 (unsigned long long)hits, (unsigned long long)misses,
                 (unsigned long long)stale, entries, DNS_CACHE_CAP);
    }
    diag_top_rules(d->rules, buf, cap, &off);

    diag_add(buf, cap, &off, "sub.user_agent", "Happ/3.26.1");
    if (d->settings.sub_ignore_gating)
        diag_add(buf, cap, &off, "sub.gating", "ignored, placeholder feeds accepted");
    diag_add(buf, cap, &off, "trace", "%s",
             d->settings.trace ? "on, one line per session event" : "off");

    diag_add(buf, cap, &off, "path.jbroot", "%s",
             SENKO_JBROOT[0] ? SENKO_JBROOT : "/");
    diag_add(buf, cap, &off, "path.hwid", "%s", SENKO_HWID_PATH);
    diag_add(buf, cap, &off, "path.log", "%s", SENKO_SYSTEM_LOG);
    diag_add(buf, cap, &off, "path.substrate", "%s", SENKO_SUBSTRATE_DIR);

    diag_add(buf, cap, &off, "proc.senkoawgd", "%s",
             proc_state("/var/run/senkoawgd.pid", scratch, sizeof scratch));
    diag_add(buf, cap, &off, "proc.senko_kick", "%s",
             kick_state(scratch, sizeof scratch));
    diag_add(buf, cap, &off, "substrate.tlsfix", "%s",
             file_state(SENKO_SUBSTRATE_DIR "/senkotlsfix.dylib", scratch, sizeof scratch));
    diag_add(buf, cap, &off, "substrate.status", "%s",
             file_state(SENKO_SUBSTRATE_DIR "/senkostatus.dylib", scratch, sizeof scratch));
    if (d->last_reason[0])
        diag_add(buf, cap, &off, "backend.last_error", "%s", d->last_reason);

    *len = off;
    return 0;
}

int daemon_ctl_fwconf(void *ctx, char *buf, size_t cap, size_t *len) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (!d || !buf || cap == 0) return -1;
    if (!d->full_device || !d->c_backend.active) return -1;
    return routing_exec_render(&d->c_backend.rules, buf, cap, len);
}

int daemon_ctl_flush(void *ctx, const char *what, char *reason, size_t reason_cap) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (!d || !what) return -1;
    if (strcmp(what, "dns") == 0) {
        if (!d->c_backend.active) {
            if (reason && reason_cap)
                snprintf(reason, reason_cap, "no dns cache on this backend");
            return -1;
        }
        routing_exec_flush_dns(&d->c_backend.rules);
        return 0;
    }
    if (strcmp(what, "bypass") == 0) {
        if (!d->c_backend.active || d->c_backend.rules.mode != ROUTING_MODE_PF) {
            if (reason && reason_cap)
                snprintf(reason, reason_cap, "no pf bypass table on this backend");
            return -1;
        }
        routing_exec_flush_bypass(&d->c_backend.rules);
        return 0;
    }
    if (strcmp(what, "config") == 0) {
/* only the knobs go back to their defaults. the catalog is the user's, and
   losing it to a settings reset is not something a confirmation covers */
        daemon_settings_defaults(&d->settings);
        senko_trace_set_enabled(d->settings.trace);
        return 0;
    }
    if (reason && reason_cap) snprintf(reason, reason_cap, "unknown flush target");
    return -1;
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
    /* a store is larger than the ios 5 stack, so control
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

/* wall-clock adjustments must not turn one tcp connect into a false latency
   spike */
static long probe_now_ms(void) {
#ifdef __APPLE__
    mach_timebase_info_data_t scale;
    if (mach_timebase_info(&scale) == KERN_SUCCESS && scale.denom != 0) {
        double ms = (double)mach_absolute_time() * (double)scale.numer /
                    (double)scale.denom / 1000000.0;
        if (ms >= 0.0 && ms <= (double)LONG_MAX) return (long)ms;
    }
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* record the step a check just finished. the slot is empty on every path but a
   check that asked for stages, so this is a branch the normal path pays and
   nothing more */
static void stage_mark(daemon_ctl_t *d, const char *name, int ok) {
    ctl_check_trace_t *trace = d ? d->trace : NULL;
    if (!trace || trace->count >= CTL_CHECK_STAGE_MAX) return;
    ctl_check_stage_t *stage = &trace->stages[trace->count++];
    snprintf(stage->name, sizeof stage->name, "%s", name ? name : "?");
    stage->ms = (int)(probe_now_ms() - trace->started_ms);
    stage->ok = ok ? 1 : 0;
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

/* a subscription host lives outside senko's own routing and address safety
   already runs over the resolved address, so the two failure modes a plain
   getaddrinfo()+connect() leaves unbounded are exactly what a hostile or just
   flaky panel host can trigger: dns that never answers, and a connect() that
   sits in the kernel's own multi-minute retry schedule. both would hang this
   fetch, its caller's REFRESH reply, and the client waiting on it, long past
   the ~15s budget the rest of subfetch is built around. */
static int subfetch_dial_direct(const char *host, uint16_t port) {
    char portstr[8];
    snprintf(portstr, sizeof portstr, "%u", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (net_getaddrinfo_timed(host, portstr, &hints, &res, 3000) != 0 || !res)
        return -1;

    int fd = -1;
    long deadline = probe_now_ms() + 5000;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        if (!net_addr_allowed(ai->ai_addr)) continue;
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK); /* keep fetch nonblocking */

        int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (r == 0) break;
        if (errno != EINPROGRESS) { close(fd); fd = -1; continue; }

        int connected = 0;
        for (;;) {
            long now = probe_now_ms();
            if (now >= deadline) break;
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            int pr = poll(&pfd, 1, (int)(deadline - now));
            if (pr < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (pr == 0) break;
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
            if (!(pfd.revents & POLLOUT)) continue;
            int soerr = 0;
            socklen_t sl = sizeof soerr;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) == 0 && soerr == 0)
                connected = 1;
            break;
        }
        if (connected) break;
        close(fd);
        fd = -1;
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
        snprintf(meta->title, sizeof meta->title, "%s", info.title);
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

/* a full-device redirect can accept the socket locally before any packet
   reaches the server. bind probes to the physical egress so their clock is
   not the utun or transparent-listener clock */
static void probe_bind_physical_interface(int fd, int full_device) {
#ifdef __APPLE__
    if (!full_device || fd < 0) return;
    char iface[32];
    char address[INET_ADDRSTRLEN];
    if (routing_exec_egress_snapshot(iface, sizeof iface,
                                     address, sizeof address) != 0)
        return;
    unsigned int index = if_nametoindex(iface);
    if (!index) return;
    if (setsockopt(fd, IPPROTO_IP, IP_BOUND_IF, &index, sizeof index) != 0)
        fprintf(stderr, "senkod: probe could not bind %s: %s\n",
                iface, strerror(errno));
#else
    (void)fd;
    (void)full_device;
#endif
}

/* measure one direct ipv4 handshake from connect until the socket reports its
   final error state */
static int probe_tcp_ipv4(const char *ip, uint16_t port, int timeout_ms,
                          int full_device) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (!ip || inet_pton(AF_INET, ip, &addr.sin_addr) != 1) return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    probe_bind_physical_interface(fd, full_device);
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        close(fd);
        return -1;
    }

    long start = probe_now_ms();
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof addr);
    if (rc != 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(fd, &writable);
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    rc = select(fd + 1, NULL, &writable, NULL, &timeout);
    if (rc <= 0 || !FD_ISSET(fd, &writable)) {
        close(fd);
        return -1;
    }

    int soerr = 0;
    socklen_t soerr_len = sizeof soerr;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &soerr_len) != 0 || soerr != 0) {
        close(fd);
        return -1;
    }
    close(fd);
    long elapsed = probe_now_ms() - start;
    return elapsed >= 0 && elapsed <= INT_MAX ? (int)elapsed : -1;
}

#define SENKO_QUIC_PROBE_LEN 1200

static void build_quic_probe_packet(unsigned char *out) {
    memset(out, 0, SENKO_QUIC_PROBE_LEN);
    out[0] = 0xc0;
    /* a reserved quic version makes a compliant server answer without auth */
    out[1] = 0x1a;
    out[2] = 0x2a;
    out[3] = 0x3a;
    out[4] = 0x4a;
    out[5] = 8;
    arc4random_buf(out + 6, 8);
    out[14] = 0;
}

static int build_salamander_packet(const char *password,
                                   const unsigned char *plain, size_t plain_len,
                                   unsigned char *wire, size_t wire_cap,
                                   size_t *wire_len) {
    unsigned char keyed[136];
    unsigned char key[32];
    size_t pass_len;
    if (!password || !plain || !wire || !wire_len) return -1;
    pass_len = strlen(password);
    if (pass_len + 8 > sizeof keyed || wire_cap < plain_len + 8) return -1;
    arc4random_buf(wire, 8);
    memcpy(keyed, password, pass_len);
    memcpy(keyed + pass_len, wire, 8);
    blake2b256(keyed, pass_len + 8, key);
    for (size_t i = 0; i < plain_len; ++i)
        wire[8 + i] = plain[i] ^ key[i % sizeof key];
    *wire_len = plain_len + 8;
    return 0;
}

static int probe_udp_ipv4(const char *ip, uint16_t port,
                          const char *obfs, const char *obfs_password,
                          int timeout_ms, int full_device) {
    unsigned char plain[SENKO_QUIC_PROBE_LEN];
    unsigned char wire[SENKO_QUIC_PROBE_LEN + 8];
    const unsigned char *packet = plain;
    size_t packet_len = sizeof plain;
    struct sockaddr_in addr;
    struct timeval timeout;
    fd_set readable;
    long started;
    int fd;

    if (!ip || !port) return -1;
    build_quic_probe_packet(plain);
    if (obfs && obfs[0] && strcmp(obfs, "none") != 0) {
        if (strcmp(obfs, "salamander") != 0 ||
            build_salamander_packet(obfs_password, plain, sizeof plain,
                                    wire, sizeof wire, &packet_len) != 0)
            return -1;
        packet = wire;
    }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) return -1;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    probe_bind_physical_interface(fd, full_device);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(fd);
        return -1;
    }
    started = probe_now_ms();
    if (send(fd, packet, packet_len, 0) != (ssize_t)packet_len) {
        close(fd);
        return -1;
    }

    FD_ZERO(&readable);
    FD_SET(fd, &readable);
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    int ready = select(fd + 1, &readable, NULL, NULL, &timeout);
    unsigned char response[64];
    ssize_t received = ready > 0 ? recv(fd, response, sizeof response, 0) : -1;
    close(fd);
    if (ready <= 0 || received <= 0) return -1;
    long elapsed = probe_now_ms() - started;
    return elapsed >= 0 && elapsed <= INT_MAX ? (int)elapsed : -1;
}

int daemon_ctl_probe(void *ctx, const char *host, uint16_t port) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    const int timeout_ms = 5000;

    char ip[INET_ADDRSTRLEN];
    if (!net_resolve_public_ipv4(host, port, ip, sizeof ip)) {
        stage_mark(d, "resolve", 0);
        return -1;
    }
    stage_mark(d, "resolve", 1);

    /* without a bypass the catch-all redirect sends the probe through the
       tunnel, so the reported latency belongs to the tunnel, not the server.
       a route-table hiccup here is not the server being unreachable, so it
       marks the stage rather than aborting: the probe still runs, just
       possibly measuring the tunnel instead of the host, same as it would if
       this backend had no bypass at all */
    int go_bypassed = 0;
    if (d && d->full_device) {
        pthread_mutex_lock(&d->probe_route_lock);
        c_backend_bypass_add_ipv4(&d->c_backend, ip);
        if (d->go.active) {
            go_bypassed = go_backend_bypass_add_ipv4(&d->go, ip) == 0;
            stage_mark(d, "route bypass", go_bypassed);
        }
        pthread_mutex_unlock(&d->probe_route_lock);
    }

    int ms = probe_tcp_ipv4(ip, port, timeout_ms, d && d->full_device);
    if (go_bypassed) {
        pthread_mutex_lock(&d->probe_route_lock);
        go_backend_bypass_remove_ipv4(&d->go, ip);
        pthread_mutex_unlock(&d->probe_route_lock);
    }
    stage_mark(d, "tcp connect", ms >= 0);
    return ms;
}

int daemon_ctl_probe_server(void *ctx, const vl_server_t *server) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (!d || !server) return -1;
    if (server->proto != VL_PROTO_HYSTERIA2)
        return daemon_ctl_probe(ctx, server->host, server->port);

    char ip[INET_ADDRSTRLEN];
    if (!net_resolve_public_ipv4(server->host, server->port,
                                 ip, sizeof ip)) {
        stage_mark(d, "resolve", 0);
        return -1;
    }
    stage_mark(d, "resolve", 1);

    int go_bypassed = 0;
    if (d->full_device) {
        pthread_mutex_lock(&d->probe_route_lock);
        c_backend_bypass_add_ipv4(&d->c_backend, ip);
        if (d->go.active)
            go_bypassed = go_backend_bypass_add_ipv4(&d->go, ip) == 0;
        pthread_mutex_unlock(&d->probe_route_lock);
    }
    int ms = probe_udp_ipv4(ip, server->port, server->obfs,
                            server->obfs_password, 2500, d->full_device);
    if (go_bypassed) {
        pthread_mutex_lock(&d->probe_route_lock);
        go_backend_bypass_remove_ipv4(&d->go, ip);
        pthread_mutex_unlock(&d->probe_route_lock);
    }
    stage_mark(d, "udp quic probe", ms >= 0);
    return ms;
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
        stage_mark(d, "probe resolve", 0);
        goto fail;
    }
    stage_mark(d, "probe resolve", 1);

    {
        long half = start + (timeout_ms * 6) / 10; /* reserve time for http */
        if (half > deadline) half = deadline;
        int fd = socks_dial_retry(d, sp, probe_ip, 443, half, &stage);
        stage_mark(d, "tunnel dial 443", fd >= 0);
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
                    stage_mark(d, "tls server hello", 1);
                    if (ms_out) *ms_out = (int)(probe_now_ms() - start);
                    return 0;
                }
                stage = got ? "bad tls response" : "no tls response";
                stage_mark(d, "tls server hello", 0);
            } else {
                stage = "probe write failed";
                stage_mark(d, "client hello write", 0);
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
        stage_mark(d, "tunnel dial 80", fd >= 0);
        if (fd < 0) goto fail;

        static const char req[] =
            "GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n";
        if (write_all_pumped_until(fd, req, sizeof req - 1, d, deadline) != 0) {
            close(fd);
            stage = "probe write failed";
            stage_mark(d, "http request write", 0);
            goto fail;
        }

        char buf[256];
        size_t got = 0;
        if (read_some_pumped(fd, buf, sizeof buf - 1, 12, d, deadline, &got) != 0) {
            close(fd);
            stage = "no http response";
            stage_mark(d, "http response", 0);
            goto fail;
        }
        close(fd);
        if (memcmp(buf, "HTTP/", 5) != 0) {
            stage = "bad http response";
            stage_mark(d, "http response", 0);
            goto fail;
        }
        stage_mark(d, "http response", 1);
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
    if (!d || !go_backend_running(&d->go)) {
        stage_mark(d, "go core running", 0);
        goto fail;
    }
    stage_mark(d, "go core running", 1);
    if (resolve_ipv4_addresses("example.com", probe_ip, sizeof probe_ip,
                               NULL, 0, 1) != 0) {
        stage = "probe DNS failed";
        stage_mark(d, "probe resolve", 0);
        goto fail;
    }
    stage_mark(d, "probe resolve", 1);
    int fd = dial_numeric_until(d, probe_ip, 80, deadline, NULL);
    stage_mark(d, "utun connect", fd >= 0);
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
        stage_mark(d, "utun http response", 0);
        goto fail;
    }
    close(fd);
    if (got < 5 || memcmp(response, "HTTP/", 5) != 0) {
        stage = "invalid data returned through TUN";
        stage_mark(d, "utun http response", 0);
        goto fail;
    }
    stage_mark(d, "utun http response", 1);
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
        status_set(0);
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
        status_set(0);
    } else {
        if (reason && reason_cap) reason[0] = '\0';
        status_set(1); /* show vpn after a real probe */
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
                        server->host, server->insecure) != LOOP_OK) {
        if (reason && reason_cap)
            snprintf(reason, reason_cap, "local test proxy could not be prepared");
        return -1;
    }
    return 0;
}

static int check_run(daemon_ctl_t *d, const char *mode, const vl_server_t *server,
                     char *reason, size_t reason_cap) {
    if (strcmp(mode, "tcp") == 0) {
        return daemon_ctl_probe_server(d, server);
    }
    if (!d->loop || !loop_listen_port(d->loop)) {
        stage_mark(d, "local proxy listening", 0);
        if (reason && reason_cap) snprintf(reason, reason_cap, "local proxy is not active");
        return -1;
    }
    stage_mark(d, "local proxy listening", 1);
    if (strcmp(mode, "proxy") == 0) {
        char numeric[INET6_ADDRSTRLEN];
        if (!net_resolve_public(server->host, server->port, numeric, sizeof numeric)) {
            stage_mark(d, "resolve", 0);
            if (reason && reason_cap) snprintf(reason, reason_cap, "unsafe or unresolved address");
            return -1;
        }
        stage_mark(d, "resolve", 1);
        long start = probe_now_ms();
        int fd = socks5_dial_via_loop(d, loop_listen_port(d->loop), numeric,
                                      server->port, start + 5000, NULL);
        stage_mark(d, "socks connect", fd >= 0);
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
            stage_mark(d, "tunnel idle", 0);
            if (reason && reason_cap)
                snprintf(reason, reason_cap, "disconnect before checking another profile");
            return -1;
        }
        stage_mark(d, "tunnel idle", 1);
        if (prepare_server_probe(d, server, reason, reason_cap) != 0) {
            stage_mark(d, "transport prepared", 0);
            return -1;
        }
        stage_mark(d, "transport prepared", 1);
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

int daemon_ctl_check(void *ctx, const char *mode, const vl_server_t *server,
                     ctl_check_trace_t *trace, char *reason, size_t reason_cap) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (!d || !mode || !server) return -1;
    if (trace) {
        memset(trace, 0, sizeof *trace);
        trace->started_ms = probe_now_ms();
    }
/* the slot is cleared on every exit, so a later probe outside a check cannot
   keep filling a trace the caller has already read */
    d->trace = trace;
    int ms = check_run(d, mode, server, reason, reason_cap);
    d->trace = NULL;
    return ms;
}
