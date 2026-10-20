#define _DEFAULT_SOURCE /* expose getaddrinfo */

#include "daemon_ctl.h"
#include "storefile.h"
#include "status.h"

#include "core/transport.h"
#include "core/transport_pick.h"
#include "core/real_probe.h"
#include "core/net_diag.h"
#include "core/senko_clock.h"
#include "core/vless.h"
#include "core/subfetch.h"
#include "core/net_safe.h"
#include "core/url.h"
#include "core/tls_clienthello.h"
#include "core/control.h"
#include "core/senko_trace.h"
#include "core/dns_cache.h"
#include "core/dns_msg.h"
#include "core/blake2b256.h"
#include "legacy_ios.h"
#include "senko_core_config.h"
#include "app_proxy.h"
#include "app_owner.h"
#include "core/routing_profile.h"
#include "direct_socket.h"
#include "direct_dns.h"
#include "egress.h"
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
#include <net/if.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#include <net/if.h>
#endif
#include <openssl/crypto.h>
#include <openssl/rand.h>

void daemon_ctl_init(daemon_ctl_t *d, loop_t *loop, const char *config_path) {
    if (!d) return;
    memset(d, 0, sizeof *d);
    pthread_mutex_init(&d->probe_route_lock, NULL);
    d->senko_core.tun_fd = -1;
    if (utun_backend_runtime_init(&d->embedded_utun) != 0)
        fprintf(stderr, "senkod: embedded utun lock initialization failed\n");
    if (awg_backend_init(&d->awg) != 0)
        fprintf(stderr, "senkod: amneziawg lock initialization failed\n");
    d->loop = loop;
    d->started_at = ctl_engine_now();
    if (config_path) {
        size_t l = strlen(config_path);
        if (l >= sizeof d->config_path) l = sizeof d->config_path - 1;
        memcpy(d->config_path, config_path, l);
        d->config_path[l] = '\0';
    }
}

void daemon_ctl_set_settings(daemon_ctl_t *d, const daemon_settings_t *s) {
    if (!d || !s) return;
    d->settings = *s;
    senko_trace_set_enabled(d->settings.trace);
}

void daemon_ctl_set_rules(daemon_ctl_t *d, ruleset_t *rules) {
    if (d) d->rules = rules;
}

void daemon_ctl_set_store(daemon_ctl_t *d, store_t *store, const char *geo_dir) {
    if (!d) return;
    d->store = store;
    snprintf(d->geo_dir, sizeof d->geo_dir, "%s", geo_dir ? geo_dir : "");
}

/* what the running tunnel reads out of the ruleset: the loaded geo sets and the
   owner lookup. both go only after the tunnel that used them has stopped */
static void release_routing(daemon_ctl_t *d) {
    /* a running amneziawg tunnel reads the same ruleset and owner lookup, and
       its own stop releases them */
    if (awg_backend_busy(&d->awg)) return;
    if (d->rules) ruleset_release_geo(d->rules);
    app_owner_close(d->owner);
    d->owner = NULL;
}

/* rebuilds the routing profile's rules from the catalog and loads the geo sets
   they name. runs with no tunnel up, because a running one reads the ruleset */
static void prepare_routing(daemon_ctl_t *d) {
    routing_info_t info;
    size_t missing = 0;
    if (!d->rules) return;
    if (d->store) {
        store_status_t status = store_apply_routing(d->store, &info);
        if (status != STORE_OK)
            fprintf(stderr, "senkod: the routing profile could not be applied (%d)\n", (int)status);
        else if (d->store->routing_active >= 0)
            fprintf(stderr, "senkod: routing profile \"%s\": %zu rule(s), %zu entr%s senko cannot match,"
                            " unmatched traffic goes %s\n",
                    info.name, info.rules, info.unsupported, info.unsupported == 1 ? "y" : "ies",
                    d->rules->fallback == RULE_ACTION_DIRECT ? "straight out" : "through the tunnel");
    }
    if (d->geo_dir[0] && ruleset_bind_geo(d->rules, d->geo_dir, &missing) != RULES_OK)
        fprintf(stderr, "senkod: the geo data could not be loaded\n");
    if (missing)
        fprintf(stderr, "senkod: %zu geo rule(s) have no data yet and match nothing until"
                        " the geo files are downloaded\n", missing);
    if (ruleset_has_type(d->rules, RULE_TYPE_APP)) {
        d->owner = app_owner_open(NULL);
        if (!d->owner)
            fprintf(stderr, "senkod: app rules need proc_info, which this system does not have;"
                            " they match nothing\n");
    }
}

void daemon_ctl_shutdown(daemon_ctl_t *d) {
    if (!d) return;
    status_set(0);
    senko_core_backend_stop(&d->senko_core);
    app_proxy_stop(&d->app_proxy);
    utun_backend_destroy(&d->embedded_utun);
    /* the profile stays recorded: the next senkod brings the tunnel back. the
       tunnel goes first because it reads the rules release_routing frees */
    awg_backend_destroy(&d->awg);
    release_routing(d);
    pthread_mutex_destroy(&d->probe_route_lock);
    OPENSSL_cleanse(&d->started, sizeof d->started);
    d->started_set = 0;
}

/* senko-core is a child process with no descriptor that closes on its exit,
   so its liveness is still sampled, once a second instead of every pass */
#define SENKO_CORE_CHECK_MS 1000

int daemon_ctl_wait_fd(const daemon_ctl_t *d) {
    return d ? utun_backend_ended_fd(&d->embedded_utun) : -1;
}

int daemon_ctl_wait_ms(const daemon_ctl_t *d) {
    return d && d->senko_core.active ? SENKO_CORE_CHECK_MS : -1;
}

int daemon_ctl_maintain(daemon_ctl_t *d) {
    if (d) utun_backend_drain_ended(&d->embedded_utun);
    if (d && d->embedded_utun.active &&
        !utun_backend_running(&d->embedded_utun)) {
        fprintf(stderr, "senkod: embedded utun packet loop exited: %s\n",
                d->embedded_utun.result.message);
        utun_backend_stop(&d->embedded_utun);
        release_routing(d);
        loop_stop(d->loop);
        status_set(0);
        return -1;
    }
    if (!d || !d->senko_core.active) return 0;
    if (senko_core_backend_running(&d->senko_core)) return 0;
    fprintf(stderr, "senkod: senko-core exited unexpectedly\n");
    senko_core_backend_stop(&d->senko_core);
    release_routing(d);
    loop_stop(d->loop);
    status_set(0);
    return -1;
}

int daemon_ctl_stats(void *ctx, uint64_t *up, uint64_t *down) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (up) *up = 0;
    if (down) *down = 0;
    if (!d || !d->loop || !up || !down) return -1;
    if (awg_backend_busy(&d->awg)) {
        awg_backend_stats(&d->awg, up, down);
        return 0;
    }
    if (d->senko_core.active) return senko_core_backend_stats(&d->senko_core, up, down);
    if (d->embedded_utun.active) {
        utun_backend_stats(&d->embedded_utun, up, down);
        return 0;
    }
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

/* the probe address is resolved before the tunnel exists. a system resolver
   that does not answer (an ios 12 device where an earlier tunnel never came up)
   left the verify with nothing to dial, and the user was told the server name
   did not resolve. the physical resolver answers it the way a subscription
   fetch does when the system one has failed */
static void resolve_probe_physical(daemon_ctl_t *d) {
    direct_socket_iface_t iface;
    char address[INET_ADDRSTRLEN];
    memset(&iface, 0, sizeof iface);
    if (egress_snapshot(iface.ifname, sizeof iface.ifname, address, sizeof address) != 0) return;
    iface.ifindex = if_nametoindex(iface.ifname);
    if (!iface.ifindex) return;
    uint32_t addresses[DNS_MSG_MAX_IPV4];
    size_t count = 0;
    if (direct_dns_ipv4(&iface, d->settings.dns_upstream, 53, "example.com", 2000,
                        addresses, DNS_MSG_MAX_IPV4, &count) != 0)
        return;
    for (size_t i = 0; i < count; ++i) {
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof sin);
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = addresses[i];
        if (!net_addr_allowed((struct sockaddr *)&sin)) continue;
        if (inet_ntop(AF_INET, &sin.sin_addr, d->probe_ip, sizeof d->probe_ip)) {
            fprintf(stderr, "senkod: probe address %s came from the physical resolver\n",
                    d->probe_ip);
            return;
        }
    }
}

int daemon_ctl_native_config(void *ctx, const vl_server_t *server, char *buf,
                             size_t cap, size_t *len) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    char endpoint[INET_ADDRSTRLEN];
    const char *ca_file = NULL;
    if (len) *len = 0;
    if (!d || !server || !buf || cap < 2 || !len)
        return -1;
    endpoint[0] = '\0';
    if (resolve_ipv4_addresses(server->host, endpoint, sizeof endpoint,
                               NULL, 0, 0) != 0)
        return -1;
    if (senko_core_roots_file(SENKO_JBROOT
            "/Applications/Senko.app/PlugIns/SenkoTunnel.appex/cacert.pem", &ca_file) != 0)
        return -1;
    /* the extension cannot read the daemon's geo directory, so geo rules stay out */
    if (senko_core_config_render_rules(server, endpoint, "utun", d->rules, NULL,
                               ca_file, buf, cap) != 0)
        return -1;
    *len = strlen(buf);
    return *len > 0 ? 0 : -1;
}

int daemon_ctl_apply(void *ctx, const ctl_action_t *action) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (!d || !d->loop || !action) return -1;

    switch (action->kind) {
        case CTL_ACT_START: {
            const vl_server_t *s = &action->server;
            d->started = *s;
            d->started_set = 1;
/* quic/udp only: no senko transport carries it, so there is nothing to hand
   the local socks loop. senko-core runs it where it can, and the utun backend
   has its own client (hy2_engine) for everything older. the connect hook
   forwards through the socks loop, which cannot speak quic */
            int quic_only = (s->proto == VL_PROTO_HYSTERIA2);
            if (quic_only && d->settings.force_backend == SENKO_BACKEND_APP_PROXY) {
                fprintf(stderr, "senkod: hysteria2 needs the utun or senko-core backend, "
                                "but the connect hook is pinned in settings\n");
                status_set(0);
                return DCTL_ERR_TRANSPORT;
            }

            const transport_vt_t *vt = quic_only ? NULL : transport_for_server(s);
            if (!vt && !quic_only) {
                fprintf(stderr, "senkod: unsupported transport/security for server\n");
                status_set(0);
                return DCTL_ERR_TRANSPORT;
            }

            uint8_t uuid[VLESS_UUID_LEN];
            memset(uuid, 0, sizeof uuid);
            if (cfg_proto_uses_uuid(s->proto)) {
                if (vless_uuid_parse(s->uuid, uuid) != VLESS_OK) {
                    fprintf(stderr, "senkod: bad uuid in server link\n");
                    status_set(0);
                    return DCTL_ERR_UUID;
                }
            }

            if (d->senko_core.active || d->app_proxy.active || d->embedded_utun.active) {
                senko_core_backend_stop(&d->senko_core);
                app_proxy_stop(&d->app_proxy);
                utun_backend_stop(&d->embedded_utun);
            }
            release_routing(d);
            prepare_routing(d);

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
                    release_routing(d);
                    status_set(0);
                    return DCTL_ERR_LOOP;
                }
            }

            char first_ip[64], ip_list[4096];
            first_ip[0] = '\0';
            ip_list[0] = '\0';

            if (resolve_ipv4_addresses(s->host, first_ip, sizeof first_ip,
                                       ip_list, sizeof ip_list, 0) != 0) {
                fprintf(stderr, "senkod: dns resolution failed for %s\n", s->host);
                release_routing(d);
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
            if (!d->probe_ip[0]) resolve_probe_physical(d);

            char backend_reason[160];
            backend_reason[0] = '\0';
            d->last_reason[0] = '\0';
            int pin = d->settings.force_backend;
/* a pin is respected even when it cannot work: the backend names the reason
   it will not run on this device, which is the answer the tester came for */
            /* On Apple A7 or iOS 12, Hysteria2 runs natively and reliably via the utun
               backend (ngtcp2/nghttp3); heavy senko-core is used only when explicitly pinned */
            int hy2_prefers_utun = (s->proto == VL_PROTO_HYSTERIA2) &&
                (senko_ios_major() <= 12 || senko_is_apple_a7());
            int core_attempted = pin == SENKO_BACKEND_CORE ||
                (pin == SENKO_BACKEND_AUTO && senko_core_backend_supported() && !hy2_prefers_utun);
/* senko-core reads geo sets from the dat files but cannot name an app, so a
   profile with app rules, or with geo rules and no dat yet, runs on the utun
   backend, which decides them itself. a
   quic only server has no utun path on ios 12 and later, and a pinned core was
   asked for by name: both keep the core and lose those rules */
            size_t core_lost = senko_core_config_unsupported(d->rules, d->geo_dir);
            if (core_attempted && core_lost) {
                if (pin == SENKO_BACKEND_AUTO && !quic_only) {
                    fprintf(stderr, "senkod: %zu geo or app rule(s) need the utun backend,"
                                    " senko-core is not used\n", core_lost);
                    core_attempted = 0;
                } else {
                    fprintf(stderr, "senkod: senko-core cannot take %zu geo or app rule(s);"
                                    " they match nothing on this backend\n", core_lost);
                }
            }
            int hook_pinned = pin == SENKO_BACKEND_APP_PROXY;
            int routing_ok = 0;
            int run_utun = 0;
            int failure = DCTL_ERR_UTUN;
            if (core_attempted) {
                failure = DCTL_ERR_CORE;
                routing_ok = senko_core_backend_start(&d->senko_core, s, first_ip,
                                              d->settings.dns_upstream, d->rules,
                                              d->geo_dir, backend_reason,
                                              sizeof backend_reason) == 0;
                if (!routing_ok) {
                    fprintf(stderr, "senkod: senko-core failed: %s\n",
                            backend_reason[0] ? backend_reason : "unknown error");
/* ios 13 rootful kills the core at exec with status 137, and an unreadable
   roots file stops it too: auto retries once on the utun backend, a pin does not */
                    if (pin == SENKO_BACKEND_AUTO) {
                        fprintf(stderr, "senkod: falling back from senko-core to the utun backend\n");
                        backend_reason[0] = '\0';
                        run_utun = 1;
                    }
                }
            } else if (hook_pinned) {
                failure = DCTL_ERR_ROUTING;
                routing_ok = app_proxy_start(&d->app_proxy, (int)loop_listen_port(d->loop),
                                             backend_reason, sizeof backend_reason) == 0;
                if (!routing_ok)
                    fprintf(stderr, "senkod: connect hook failed: %s\n", backend_reason);
            } else {
                run_utun = 1;
            }
            if (run_utun) {
                failure = DCTL_ERR_UTUN;
                utun_backend_set_app_owner(&d->embedded_utun,
                                           d->owner ? app_owner_policy_lookup : NULL, d->owner);
                routing_ok = utun_backend_start(&d->embedded_utun, s, ip_list,
                    d->rules, d->settings.dns_upstream,
                    d->settings.block_response, backend_reason,
                    sizeof backend_reason) == 0;
                if (!routing_ok)
                    fprintf(stderr, "senkod: utun tunnel failed: %s\n",
                            backend_reason[0] ? backend_reason : "unknown error");
/* hooked apps still get through when no tunnel opens, as the last rung of the
   old firewall ladder gave them; only auto may take it instead of the pin */
                if (!routing_ok && !quic_only && pin == SENKO_BACKEND_AUTO &&
                    app_proxy_available()) {
                    char hook_reason[160];
                    if (app_proxy_start(&d->app_proxy, (int)loop_listen_port(d->loop),
                                        hook_reason, sizeof hook_reason) == 0) {
                        fprintf(stderr, "senkod: only hooked apps are carried, through"
                                        " the connect hook\n");
                        routing_ok = 1;
                    }
                }
            }
            if (!routing_ok) {
                snprintf(d->last_reason, sizeof d->last_reason, "%s",
                         backend_reason);
                release_routing(d);
                loop_stop(d->loop);
                status_set(0);
                return failure;
            }
            if (d->app_proxy.active && backend_reason[0])
                snprintf(d->last_reason, sizeof d->last_reason, "%s", backend_reason);
            return 0;
        }

        case CTL_ACT_STOP:
            OPENSSL_cleanse(&d->started, sizeof d->started);
            d->started_set = 0;
            /* the badge belongs to amneziawg while that tunnel runs */
            if (!awg_backend_busy(&d->awg)) status_set(0);
            senko_core_backend_stop(&d->senko_core);
            app_proxy_stop(&d->app_proxy);
            utun_backend_stop(&d->embedded_utun);
            release_routing(d);
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

/* the rules that are doing something. a rule list nobody can see hit counts for
   is a list of guesses */
/* the counters pf, its dns forwarder and its bypass table used to show */
static void diag_utun(daemon_ctl_t *d, char *buf, size_t cap, size_t *off) {
    utun_backend_t *u = &d->embedded_utun;
    diag_add(buf, cap, off, "utun.routes", "%zu route(s), physical %s",
             u->plan.count, u->plan.physical_ifname[0] ? u->plan.physical_ifname : "?");
    if (u->dns_state[0]) diag_add(buf, cap, off, "utun.dns", "%s", u->dns_state);
    utun_backend_snapshot_t snap;
    if (utun_backend_snapshot(u, &snap) != 0) {
        diag_add(buf, cap, off, "utun.counters", "the tunnel thread did not answer in 1 s");
        return;
    }
    diag_add(buf, cap, off, "utun.tcp",
             "%llu opened, %llu refused, %llu reset, %llu finished, %llu direct, %llu blocked",
             (unsigned long long)snap.tcp.opened, (unsigned long long)snap.tcp.refused,
             (unsigned long long)snap.tcp.aborted, (unsigned long long)snap.tcp.finished,
             (unsigned long long)snap.tcp.direct, (unsigned long long)snap.tcp.blocked);
    diag_add(buf, cap, off, "utun.tcp.bytes", "%llu up, %llu down",
             (unsigned long long)snap.tcp.bytes_to_server,
             (unsigned long long)snap.tcp.bytes_to_app);
    if (snap.tcp.last_error[0])
        diag_add(buf, cap, off, "utun.tcp.last_error", "%s", snap.tcp.last_error);
    diag_add(buf, cap, off, "utun.udp",
             "%llu in, %llu out, %llu direct, %llu blocked, %llu refused, %llu expired",
             (unsigned long long)snap.udp.received, (unsigned long long)snap.udp.sent,
             (unsigned long long)snap.udp.direct, (unsigned long long)snap.udp.blocked,
             (unsigned long long)snap.udp.refused, (unsigned long long)snap.udp.expired);
    if (snap.udp.last_error[0])
        diag_add(buf, cap, off, "utun.udp.last_error", "%s", snap.udp.last_error);
    diag_add(buf, cap, off, "utun.packets",
             "%llu in, %llu out, %llu queued, %llu queue full, %llu write error(s)",
             (unsigned long long)snap.stack.frames_in, (unsigned long long)snap.stack.frames_out,
             (unsigned long long)snap.stack.queued, (unsigned long long)snap.stack.queue_full,
             (unsigned long long)snap.stack.write_errors);
    for (int i = 1; i < TUN_NAT_DROP_REASON_COUNT; ++i)
        if (snap.nat_dropped[i])
            diag_add(buf, cap, off, "utun.dropped", "%llu: %s",
                     (unsigned long long)snap.nat_dropped[i],
                     tun_nat_drop_text((tun_nat_drop_t)i));
    diag_add(buf, cap, off, "dns.queries",
             "%llu, %llu blocked, %llu cached, %llu stale, %llu unanswered",
             (unsigned long long)snap.policy.dns_queries,
             (unsigned long long)snap.policy.dns_blocked,
             (unsigned long long)snap.policy.dns_cache_hits,
             (unsigned long long)snap.policy.dns_stale_answers,
             (unsigned long long)snap.policy.dns_failed);
    diag_add(buf, cap, off, "dns.cache", "%llu hit, %llu miss, %llu stale, %zu of %d entries",
             (unsigned long long)snap.dns_cache_hits, (unsigned long long)snap.dns_cache_misses,
             (unsigned long long)snap.dns_cache_stale, snap.dns_cache_entries, DNS_CACHE_CAP);
    diag_add(buf, cap, off, "rules.flows", "%llu proxy, %llu direct, %llu blocked",
             (unsigned long long)snap.policy.flows_proxy,
             (unsigned long long)snap.policy.flows_direct,
             (unsigned long long)snap.policy.flows_blocked);
    if (snap.direct_map_enabled)
        diag_add(buf, cap, off, "rules.direct_addresses",
                 "%zu known, %zu direct, %llu dropped by capacity",
                 snap.direct_map.addresses, snap.direct_map.direct,
                 (unsigned long long)snap.direct_map_evicted);
}

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

/* which profile the rules came from, and how well the app lookup is working: a
   lookup that finds nothing looks the same as an app nobody ruled, so its
   counters are what tell the two apart */
static void diag_routing(const daemon_ctl_t *d, char *buf, size_t cap, size_t *off) {
    if (d->store && d->store->routing_active >= 0)
        diag_add(buf, cap, off, "routing.profile", "subscription %d, unmatched traffic goes %s",
                 d->store->routing_active,
                 d->rules && d->rules->fallback == RULE_ACTION_DIRECT ? "straight out"
                                                                      : "through the tunnel");
    if (d->owner) {
        app_owner_stats_t st;
        app_owner_stats(d->owner, &st);
        diag_add(buf, cap, off, "rules.apps",
                 "%llu named, %llu outside any app, %llu unknown, %zu socket(s), %llu scan(s), %llu error(s)",
                 (unsigned long long)st.named, (unsigned long long)st.no_bundle,
                 (unsigned long long)st.unowned, (size_t)st.sockets,
                 (unsigned long long)st.scans, (unsigned long long)st.errors);
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
    if (d->embedded_utun.active)
        diag_add(buf, cap, &off, "backend", "utun tunnel on %s",
                 d->embedded_utun.plan.ifname);
    else if (d->senko_core.active)
        diag_add(buf, cap, &off, "backend", "senko-core on %s", d->senko_core.route.ifname);
    else if (d->app_proxy.active)
        diag_add(buf, cap, &off, "backend", "connect hook, hooked apps only");
    else
        diag_add(buf, cap, &off, "backend", "idle");
    diag_add(buf, cap, &off, "backend.senko_core_supported", "%s",
             senko_core_backend_supported() ? "yes" : sizeof(void *) == 8
                 ? "no, ios below 12" : "no, 32 bit slice");
    if (d->settings.force_backend != SENKO_BACKEND_AUTO)
        diag_add(buf, cap, &off, "backend.pinned", "%s",
                 daemon_settings_backend_name(d->settings.force_backend));
    if (d->embedded_utun.active) diag_utun(d, buf, cap, &off);
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
    diag_top_rules(d->rules, buf, cap, &off);
    diag_routing(d, buf, cap, &off);

    diag_add(buf, cap, &off, "sub.user_agent", "Happ/3.26.1/ios");
    if (d->settings.sub_ignore_gating)
        diag_add(buf, cap, &off, "sub.gating", "ignored, placeholder feeds accepted");
    diag_add(buf, cap, &off, "trace", "%s",
             d->settings.trace ? "on, one line per session event" : "off");

    diag_add(buf, cap, &off, "path.jbroot", "%s",
             SENKO_JBROOT[0] ? SENKO_JBROOT : "/");
    diag_add(buf, cap, &off, "path.hwid", "%s", SENKO_HWID_PATH);
    diag_add(buf, cap, &off, "path.log", "%s", SENKO_SYSTEM_LOG);
    diag_add(buf, cap, &off, "path.substrate", "%s", SENKO_SUBSTRATE_DIR);

    int awg_facts = awg_backend_describe(&d->awg, -1, NULL, 0, NULL, 0);
    for (int i = 0; i < awg_facts; ++i) {
        char key[32], value[192];
        awg_backend_describe(&d->awg, i, key, sizeof key, value, sizeof value);
        diag_add(buf, cap, &off, key, "%s", value);
    }
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
    return utun_backend_render_routes(&d->embedded_utun, buf, cap, len);
}

int daemon_ctl_flush(void *ctx, const char *what, char *reason, size_t reason_cap) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (!d || !what) return -1;
    if (strcmp(what, "dns") == 0) {
        if (utun_backend_flush_dns(&d->embedded_utun) != 0) {
            if (reason && reason_cap)
                snprintf(reason, reason_cap, "no dns cache: the utun tunnel is not running");
            return -1;
        }
        return 0;
    }
    /* "bypass" is the name the pf table had; it is the direct address table now */
    if (strcmp(what, "bypass") == 0 || strcmp(what, "direct") == 0) {
        if (utun_backend_flush_direct(&d->embedded_utun) != 0) {
            if (reason && reason_cap)
                snprintf(reason, reason_cap,
                         "no direct address table: the utun tunnel is not running");
            return -1;
        }
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

/* the utun split defaults route plain sockets back into the tunnel, so a
   direct retry must use the same physical interface binding as direct flows */
static int subfetch_connect_bound(const direct_socket_iface_t *iface,
                                   const struct sockaddr *addr, socklen_t addr_len,
                                   long deadline) {
    int fd = direct_socket_bound((void *)iface, addr->sa_family, SOCK_STREAM, NULL, 0);
    if (fd < 0) return -1;
    if (connect(fd, addr, addr_len) == 0) return fd;
    if (errno == EINPROGRESS) {
        for (;;) {
            long left = deadline - probe_now_ms();
            if (left <= 0) break;
            struct pollfd pfd = { fd, POLLOUT, 0 };
            int pr = poll(&pfd, 1, (int)left);
            if (pr < 0 && errno == EINTR) continue;
            if (pr > 0 && (pfd.revents & POLLOUT) &&
                !(pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                int error = 0;
                socklen_t size = sizeof error;
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) == 0 && error == 0)
                    return fd;
            }
            break;
        }
    }
    close(fd);
    return -1;
}

static int subfetch_dial_direct(daemon_ctl_t *d, const char *host, uint16_t port,
                                int budget_ms, int tunnel_failed) {
    if (budget_ms <= 0) return -1;
    long deadline = probe_now_ms() + budget_ms;

    direct_socket_iface_t iface;
    memset(&iface, 0, sizeof iface);
    if (egress_interface(iface.ifname, sizeof iface.ifname) != 0) {
        fprintf(stderr, "senkod: direct dial to %s has no wifi or cellular interface with a global address\n", host);
        return -1;
    }
    iface.ifindex = if_nametoindex(iface.ifname);
    if (!iface.ifindex) {
        fprintf(stderr, "senkod: direct dial to %s: interface %s has no index\n", host, iface.ifname);
        return -1;
    }

    char portstr[8];
    snprintf(portstr, sizeof portstr, "%u", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
/* leave the connect at least as much room as the name lookup gets */
    int dns_ms = budget_ms / 2;
    if (dns_ms > 3000) dns_ms = 3000;
    if (tunnel_failed && dns_ms > 1000) dns_ms = 1000;
    if (dns_ms <= 0) dns_ms = 1;
    int resolved = net_getaddrinfo_timed(host, portstr, &hints, &res, dns_ms) == 0 && res;
    if (!resolved)
        fprintf(stderr, "senkod: system dns failed for %s; trying physical resolver\n", host);

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        char shown[INET6_ADDRSTRLEN] = "?";
        if (ai->ai_family == AF_INET)
            inet_ntop(AF_INET, &((struct sockaddr_in *)ai->ai_addr)->sin_addr, shown, sizeof shown);
        else if (ai->ai_family == AF_INET6)
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr, shown, sizeof shown);
        if (!net_addr_allowed(ai->ai_addr)) {
            fprintf(stderr, "senkod: direct dial to %s refused address %s\n", host, shown);
            continue;
        }
        fd = subfetch_connect_bound(&iface, ai->ai_addr, ai->ai_addrlen, deadline);
        if (fd >= 0) break;
        fprintf(stderr, "senkod: direct dial to %s could not connect to %s on %s\n",
                host, shown, iface.ifname);
    }
    if (res) freeaddrinfo(res);
    if (fd < 0 && !resolved && d && deadline > probe_now_ms()) {
        uint32_t addresses[DNS_MSG_MAX_IPV4];
        size_t count = 0;
        int remaining = (int)(deadline - probe_now_ms());
        if (direct_dns_ipv4(&iface, d->settings.dns_upstream, 53, host,
                            remaining, addresses, DNS_MSG_MAX_IPV4, &count) == 0) {
            for (size_t i = 0; i < count && fd < 0; ++i) {
                struct sockaddr_in addr;
                memset(&addr, 0, sizeof addr);
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port);
                addr.sin_addr.s_addr = addresses[i];
                fd = subfetch_connect_bound(&iface, (struct sockaddr *)&addr,
                                            sizeof addr, deadline);
            }
        }
    }
    return fd;
}

/* a selected server that is up enough to accept a local socks5 handshake but
   then drops the upstream session mid-fetch never trips the fd<0 fallback
   below: the dial itself looked fine, the failure only shows up later, deep
   inside subfetch's own read loop. force_direct lets daemon_ctl_fetch retry
   the whole fetch bypassing the tunnel once that happens, instead of leaving
   a dead server able to block every subscription refresh along with it */
typedef struct {
    void *real_ctx;
    int force_direct;
    int last_via_tunnel;
} fetch_dial_ctx_t;

static int subfetch_dial(void *ctx, const char *host, uint16_t port, int budget_ms) {
    fetch_dial_ctx_t *w = (fetch_dial_ctx_t *)ctx;
    daemon_ctl_t *d = w ? (daemon_ctl_t *)w->real_ctx : NULL;
    if (w) w->last_via_tunnel = 0;
    if (!w || !w->force_direct) {
        if (d && d->app_proxy.active && d->loop) {
            uint16_t sp = loop_listen_port(d->loop);
            if (sp) {
/* the tunnel gets half of what is left: a selected server that accepts the
   local socks5 handshake and then stalls used to burn a fixed 8s here on top
   of the fetch timeout, so the direct fallback below never got to run inside
   the deadline the app waits on */
                int share = budget_ms / 2;
                if (share > 0) {
                    long started = probe_now_ms();
                    int fd = socks5_dial_via_loop(NULL, sp, host, port,
                                                  started + share, NULL);
                    if (fd >= 0) {
                        w->last_via_tunnel = 1;
                        return fd;
                    }
                    budget_ms -= (int)(probe_now_ms() - started);
                }
            }
        }
    }
    return subfetch_dial_direct(d, host, port, budget_ms, w->force_direct);
}

/* the user's words for a failed download. what names the thing fetched */
static void fetch_error_text(subfetch_status_t r, const char *host, int http_status,
                             const char *what, char *e, size_t ec) {
    switch (r) {
        case SUBFETCH_ERR_DIAL:
            snprintf(e, ec, "cannot resolve or connect to %.96s", host); break;
        case SUBFETCH_ERR_TRANSPORT:
            snprintf(e, ec, "tls or connection to %.96s failed", host); break;
        case SUBFETCH_ERR_HTTP:
            if (http_status && (http_status < 200 || http_status >= 300))
                snprintf(e, ec, "%.96s answered HTTP %d", host, http_status);
            else
                snprintf(e, ec, "%.96s sent a response senko cannot read", host);
            break;
        case SUBFETCH_ERR_TOOBIG:
            snprintf(e, ec, "the %s from %.96s is too large", what, host); break;
        case SUBFETCH_ERR_REDIRECT:
            snprintf(e, ec, "%.96s redirected too often or to an unusable address", host); break;
        default:
            snprintf(e, ec, "the %s url is invalid", what); break;
    }
}

/* runs on a ctl_server fetch thread while the main loop keeps serving the
   local socks port, so nothing here may step the loop */
int daemon_ctl_fetch(void *ctx, const char *url,
                     const char *request_header,
                     unsigned char *buf, size_t cap, size_t *len,
                     ctl_fetch_meta_t *meta) {
    fetch_dial_ctx_t dial_ctx = { ctx, 0, 0 };
    subfetch_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.dial = subfetch_dial;
    cfg.dial_ctx = &dial_ctx;
    cfg.tcp = &transport_tcp;
    cfg.tls = &transport_tls; /* use tls for https */
    cfg.request_header = request_header;
    cfg.max_redirects = 5;
/* the app waits 20s for the reply, and a failed tunnel attempt is retried
   direct inside that window, so only a fetch that cannot touch the tunnel
   may run past its 8s */
    {
        daemon_ctl_t *dctl = (daemon_ctl_t *)ctx;
        cfg.progress_extend_ms = (dctl && dctl->app_proxy.active && dctl->loop) ? 0 : 10000;
    }

/* the app gives REFRESH/ADDSUB 20s before it gives up waiting on this reply,
   and a retry means the deadline gets spent twice: budget each attempt short
   enough that a dead-tunnel-then-direct round trip still answers in time
   instead of trading a fast "ERR" for a slow client-side timeout */
    subfetch_info_t info;
    subfetch_status_t r = subfetch_get_info(&cfg, url, buf, cap, len, 8000, &info);
/* the selected server dying mid-fetch and a genuinely unreachable url look
   identical here (dial/transport failure), so retry once bypassing the
   tunnel before giving up: a dead server must not also block every
   subscription refresh that could otherwise replace it */
    if ((r == SUBFETCH_ERR_DIAL || r == SUBFETCH_ERR_TRANSPORT) &&
        dial_ctx.last_via_tunnel) {
        dial_ctx.force_direct = 1;
        fprintf(stderr, "senkod: subfetch retrying direct, bypassing the tunnel\n");
        cfg.progress_extend_ms = 0;
        r = subfetch_get_info(&cfg, url, buf, cap, len, 8000, &info);
    }
    if (r != SUBFETCH_OK) {
        url_t eu;
        const char *host = url && url_parse(url, &eu) == URL_OK ? eu.host : "the server";
        if (meta) fetch_error_text(r, host, info.http_status, "subscription",
                                   meta->error, sizeof meta->error);
        if (meta && info.timed_out) {
            if (info.http_status)
                snprintf(meta->error, sizeof meta->error,
                         "%.96s timed out after HTTP %d (%zu body bytes)",
                         host, info.http_status, info.received);
            else
                snprintf(meta->error, sizeof meta->error,
                         "%.96s timed out before the HTTP response", host);
        }
        const char *why = "unknown";
        switch (r) {
            case SUBFETCH_ERR_ARG:       why = "bad arg"; break;
            case SUBFETCH_ERR_URL:        why = "bad url"; break;
            case SUBFETCH_ERR_DIAL:       why = "dns/connect"; break;
            case SUBFETCH_ERR_TRANSPORT: why = "tls/io"; break;
            case SUBFETCH_ERR_HTTP:       why = "http status/body"; break;
            case SUBFETCH_ERR_TOOBIG:     why = "body too big"; break;
            case SUBFETCH_ERR_REDIRECT:   why = "redirect"; break;
            default: break;
        }
/* redaction prevents subscription credentials from reaching system logs */
        url_t u;
        if (url && url_parse(url, &u) == URL_OK)
            fprintf(stderr, "senkod: subfetch failed: %s (rc=%d, http %d, body %zu, timeout %d) %s://%s:%u/...\n",
                    why, (int)r, info.http_status, info.received, info.timed_out,
                    u.is_https ? "https" : "http", u.host,
                    (unsigned)u.port);
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
        meta->body_cut = info.body_cut;
        snprintf(meta->routing, sizeof meta->routing, "%s", info.routing);
        meta->routing_too_long = info.routing_too_long;
        snprintf(meta->gate_reason, sizeof meta->gate_reason, "%s", info.gate_reason);
    }
    fprintf(stderr, "senkod: subfetch ok %zu bytes\n", len ? *len : 0);
    return 0;
}

/* a geo dat is tens of megabytes, so it goes to a file as it arrives. happ gives
   a download three minutes, and so does this. like a subscription it goes through
   the tunnel when one is up and retries direct when that dial or handshake failed */
int daemon_ctl_geo_fetch(void *ctx, const char *url, FILE *out, size_t max_bytes,
                         size_t *written, char *error, size_t error_cap) {
    fetch_dial_ctx_t dial_ctx = { ctx, 0, 0 };
    subfetch_cfg_t cfg;
    subfetch_info_t info;
    memset(&cfg, 0, sizeof cfg);
    cfg.dial = subfetch_dial;
    cfg.dial_ctx = &dial_ctx;
    cfg.tcp = &transport_tcp;
    cfg.tls = &transport_tls;
    cfg.max_redirects = 5;
    subfetch_status_t r = subfetch_get_file(&cfg, url, out, max_bytes, written, 180000, &info);
    if ((r == SUBFETCH_ERR_DIAL || r == SUBFETCH_ERR_TRANSPORT) && dial_ctx.last_via_tunnel) {
        /* start the file over: the failed attempt may have written some of it */
        dial_ctx.force_direct = 1;
        fprintf(stderr, "senkod: geo download retrying direct, bypassing the tunnel\n");
        if (fflush(out) != 0 || ftruncate(fileno(out), 0) != 0 || fseek(out, 0, SEEK_SET) != 0) {
            snprintf(error, error_cap, "the geo download file could not be reset");
            return -1;
        }
        r = subfetch_get_file(&cfg, url, out, max_bytes, written, 180000, &info);
    }
    if (r == SUBFETCH_OK) {
        fprintf(stderr, "senkod: geo download ok %zu bytes\n", *written);
        return 0;
    }
    {
        url_t u;
        const char *host = url && url_parse(url, &u) == URL_OK ? u.host : "the server";
        /* a sink that failed reads as an unreadable response; the disk is the cause */
        if (r == SUBFETCH_ERR_HTTP && !info.http_status && ferror(out))
            snprintf(error, error_cap, "the geo file could not be written, the disk may be full");
        else
            fetch_error_text(r, host, info.http_status, "geo file", error, error_cap);
        fprintf(stderr, "senkod: geo download failed: %s\n", error);
    }
    return -1;
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

/* the tunnel's redirect can accept the socket locally before any packet
   reaches the server. bind probes to the physical egress so their clock is
   not the utun or transparent-listener clock */
static void probe_bind_physical_interface(int fd) {
#ifdef __APPLE__
    if (fd < 0) return;
    char iface[32];
    char address[INET_ADDRSTRLEN];
    if (egress_snapshot(iface, sizeof iface, address, sizeof address) != 0)
        return;
    unsigned int index = if_nametoindex(iface);
    if (!index) return;
    if (setsockopt(fd, IPPROTO_IP, IP_BOUND_IF, &index, sizeof index) != 0)
        fprintf(stderr, "senkod: probe could not bind %s: %s\n",
                iface, strerror(errno));
#else
    (void)fd;
#endif
}

/* measure one direct ipv4 handshake from connect until the socket reports its
   final error state */
static int probe_tcp_ipv4(const char *ip, uint16_t port, int timeout_ms) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (!ip || inet_pton(AF_INET, ip, &addr.sin_addr) != 1) return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    probe_bind_physical_interface(fd);
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
                          int timeout_ms) {
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
    probe_bind_physical_interface(fd);
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
    int core_bypassed = 0;
    if (d) {
        pthread_mutex_lock(&d->probe_route_lock);
        if (d->senko_core.active) {
            core_bypassed = senko_core_backend_bypass_add_ipv4(&d->senko_core, ip) == 0;
            stage_mark(d, "route bypass", core_bypassed);
        }
        pthread_mutex_unlock(&d->probe_route_lock);
    }

    int ms = probe_tcp_ipv4(ip, port, timeout_ms);
    if (core_bypassed) {
        pthread_mutex_lock(&d->probe_route_lock);
        senko_core_backend_bypass_remove_ipv4(&d->senko_core, ip);
        pthread_mutex_unlock(&d->probe_route_lock);
    }
    stage_mark(d, "tcp connect", ms >= 0);
    return ms;
}

/* senko-core routes everything into its tunnel, the physical interface binding
   notwithstanding, so a direct probe needs the address carved out */
static int probe_bypass_add(daemon_ctl_t *d, const char *ip) {
    int bypassed = 0;
    pthread_mutex_lock(&d->probe_route_lock);
    if (d->senko_core.active)
        bypassed = senko_core_backend_bypass_add_ipv4(&d->senko_core, ip) == 0 ? 1 : -1;
    pthread_mutex_unlock(&d->probe_route_lock);
    return bypassed;
}

static void probe_bypass_remove(daemon_ctl_t *d, const char *ip, int bypassed) {
    if (bypassed <= 0) return;
    pthread_mutex_lock(&d->probe_route_lock);
    senko_core_backend_bypass_remove_ipv4(&d->senko_core, ip);
    pthread_mutex_unlock(&d->probe_route_lock);
}

/* vmess, reality and certificate dates fail when the device clock is off, and
   look like a broken server when they do; a direct answer from the probe host
   estimates the difference, without changing the device clock */
static void probe_measure_clock(daemon_ctl_t *d) {
    char ip[INET_ADDRSTRLEN];
    if (!net_resolve_public_ipv4(REAL_PROBE_HOST, REAL_PROBE_PORT, ip, sizeof ip)) return;
    int bypassed = probe_bypass_add(d, ip);
    if (bypassed < 0) return;
    int measured = real_probe_clock(ip, probe_bind_physical_interface, 3000) == 0;
    probe_bypass_remove(d, ip, bypassed);
    stage_mark(d, "device clock", measured);
}

/* the list ping and the server checks. a bare tcp connect reported the nearest
   cdn edge for nodes behind one (2 to 8 ms for a server abroad) and whatever
   the wifi radio added while it dozed, so the number is the delay of a real
   request carried through the server instead. a failure leaves in reason what
   is wrong, in words the app translates */
static int diagnose_server(daemon_ctl_t *d, const vl_server_t *server, int timeout_ms, int tcp_only,
                           char *reason, size_t reason_cap) {
    net_diag_t diag;
    memset(&diag, 0, sizeof diag);
    diag.proto = server->proto;
    diag.port = server->port;
    char ip[INET_ADDRSTRLEN];
    if (!net_resolve_public_ipv4(server->host, server->port, ip, sizeof ip)) {
        stage_mark(d, "resolve", 0);
        diag.stage = NET_DIAG_DNS;
        if (reason && reason_cap) net_diag_text(&diag, reason, reason_cap);
        return -1;
    }
    stage_mark(d, "resolve", 1);

    int bypassed = probe_bypass_add(d, ip);
    if (d->senko_core.active) stage_mark(d, "route bypass", bypassed > 0);
    if (bypassed < 0) {
        if (reason && reason_cap)
            snprintf(reason, reason_cap, "this device could not open the connection: restart Senko and check System Logs");
        return -1;
    }
    char text[256];
    int ms = tcp_only
        ? real_probe_tcp(server, ip, probe_bind_physical_interface, timeout_ms, text, sizeof text)
        : real_probe_run(server, ip, probe_bind_physical_interface, timeout_ms,
                         &diag, text, sizeof text);
    probe_bypass_remove(d, ip, bypassed);
    if (!tcp_only && ms < 0 && net_diag_wants_clock(&diag)) {
        probe_measure_clock(d);
        if (senko_clock_offset(&diag.clock_offset_s)) {
            diag.clock_known = 1;
            net_diag_text(&diag, text, sizeof text);
        }
    }
    stage_mark(d, "answer through the server", ms >= 0);
    if (ms < 0) fprintf(stderr, "senkod: server %s:%u check failed: %s\n",
                        server->host, (unsigned)server->port, text);
    if (reason && reason_cap) snprintf(reason, reason_cap, "%s", ms >= 0 ? "" : text);
    return ms;
}

int daemon_ctl_probe_server(void *ctx, const vl_server_t *server) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (!d || !server) return -1;
    if (server->proto != VL_PROTO_HYSTERIA2)
        return diagnose_server(d, server, 6000, 0, NULL, 0);

    char ip[INET_ADDRSTRLEN];
    if (!net_resolve_public_ipv4(server->host, server->port,
                                 ip, sizeof ip)) {
        stage_mark(d, "resolve", 0);
        return -1;
    }
    stage_mark(d, "resolve", 1);

    int core_bypassed = 0;
    pthread_mutex_lock(&d->probe_route_lock);
    if (d->senko_core.active)
        core_bypassed = senko_core_backend_bypass_add_ipv4(&d->senko_core, ip) == 0;
    pthread_mutex_unlock(&d->probe_route_lock);
    int ms = probe_udp_ipv4(ip, server->port, server->obfs,
                            server->obfs_password, 2500);
    if (core_bypassed) {
        pthread_mutex_lock(&d->probe_route_lock);
        senko_core_backend_bypass_remove_ipv4(&d->senko_core, ip);
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
static int utun_carry_probe(daemon_ctl_t *d, int timeout_ms, int *ms_out,
                          char *stage_out, size_t stage_cap) {
    long start = probe_now_ms();
    long deadline = start + timeout_ms;
    const char *stage = "utun backend is not running";
    if (ms_out) *ms_out = -1;
    if (stage_out && stage_cap) stage_out[0] = '\0';
    if (!d || !(d->senko_core.active ? senko_core_backend_running(&d->senko_core) :
                 utun_backend_running(&d->embedded_utun))) {
        stage_mark(d, "utun backend running", 0);
        goto fail;
    }
    stage_mark(d, "utun backend running", 1);
    /* resolving after utun starts can send this lookup through the very
       tunnel being checked, so use the public address saved before routing */
    if (!d->probe_ip[0]) {
        stage = "probe address unavailable before tunnel start";
        stage_mark(d, "probe resolve", 0);
        goto fail;
    }
    stage_mark(d, "probe resolve", 1);
    /* a working proxy can block destination port 80 while carrying TLS */
    long tls_deadline = start + (timeout_ms * 6) / 10;
    if (tls_deadline > deadline) tls_deadline = deadline;
    int fd = dial_numeric_until(d, d->probe_ip, 443, tls_deadline, NULL);
    stage_mark(d, "utun tls connect", fd >= 0);
    if (fd >= 0) {
        uint8_t hello[2100];
        size_t hello_len = 0;
        if (build_probe_clienthello_record(hello, sizeof hello, &hello_len,
                                           "example.com") == 0 &&
            write_all_pumped_until(fd, hello, hello_len, d, tls_deadline) == 0) {
            char response[64];
            size_t got = 0;
            if (read_some_pumped(fd, response, sizeof response, 5, d,
                                 tls_deadline, &got) == 0 &&
                is_tls_record_prefix(response, got)) {
                close(fd);
                stage_mark(d, "utun tls response", 1);
                if (ms_out) *ms_out = (int)(probe_now_ms() - start);
                return 0;
            }
        }
        close(fd);
        stage_mark(d, "utun tls response", 0);
    }

    fd = dial_numeric_until(d, d->probe_ip, 80, deadline, NULL);
    stage_mark(d, "utun http connect", fd >= 0);
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

/* each backend is proven by a live connection in the carry probe; here only
   whether the one that should run still does */
static int backend_path_probe(daemon_ctl_t *d) {
    if (!d) return 0;
    if (d->embedded_utun.active)
        return utun_backend_running(&d->embedded_utun) ? 0 : -1;
    if (d->senko_core.active) return senko_core_backend_running(&d->senko_core) ? 0 : -1;
    return d->app_proxy.active ? 0 : -1;
}

int daemon_ctl_verify_tunnel(void *ctx, char *reason, size_t reason_cap) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (backend_path_probe(d) != 0) {
        static const char failure[] =
            "routing: the tunnel stopped before its check could run";
        fprintf(stderr, "senkod: routing verification failed: %s\n", failure);
        if (reason && reason_cap) snprintf(reason, reason_cap, "%s", failure);
        status_set(0);
        return -1;
    }
    const int timeout_ms = 8000;
    char stage[120];
    stage[0] = '\0';
    int r = d && (d->senko_core.active || d->embedded_utun.active)
        ? utun_carry_probe(d, timeout_ms, NULL, stage, sizeof stage)
        : tunnel_carry_probe(d, timeout_ms, NULL, stage, sizeof stage);
    if (r != 0) {
        fprintf(stderr, "senkod: tunnel verify failed: %s (%dms)\n",
                stage[0] ? stage : "unknown", timeout_ms);
        /* the tunnel only sees requests fail; a direct probe of the server
           names why. when the server answers directly, the fault is local
           and the tunnel's own stage stays */
        char why[256];
        why[0] = '\0';
        if (d->started_set && d->started.proto != VL_PROTO_HYSTERIA2 &&
            diagnose_server(d, &d->started, 5000, 0, why, sizeof why) < 0 && why[0]) {
            if (reason && reason_cap) snprintf(reason, reason_cap, "server: %s", why);
        } else if (reason && reason_cap) {
            snprintf(reason, reason_cap, "%s",
                     stage[0] ? stage : "tunnel verify failed");
        }
        status_set(0);
    } else {
        if (reason && reason_cap) reason[0] = '\0';
        /* show vpn after a real probe */
        if (d && d->embedded_utun.active) status_set_on(d->embedded_utun.direct.ifname);
        else status_set(1);
    }
    return r;
}

int daemon_ctl_ping_tunnel(void *ctx) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    const int timeout_ms = 4000;
    int ms = -1;
    int rc = d && (d->senko_core.active || d->embedded_utun.active)
        ? utun_carry_probe(d, timeout_ms, &ms, NULL, 0)
        : tunnel_carry_probe(d, timeout_ms, &ms, NULL, 0);
    return rc == 0 ? ms : -1;
}

static int check_run(daemon_ctl_t *d, const char *mode, const vl_server_t *server,
                     char *reason, size_t reason_cap) {
    /* the tcp check stays a bare handshake: it tells a blocked port apart
       from a server that refuses the proxy, which the list ping cannot */
    if (strcmp(mode, "tcp") == 0) {
        if (server->proto == VL_PROTO_HYSTERIA2) return daemon_ctl_probe_server(d, server);
        return diagnose_server(d, server, 6000, 1, reason, reason_cap);
    }
    if (strcmp(mode, "real") == 0) {
        if (server->proto != VL_PROTO_HYSTERIA2)
            return diagnose_server(d, server, 6000, 0, reason, reason_cap);
        int ms = daemon_ctl_probe_server(d, server);
        if (ms < 0 && reason && reason_cap)
            snprintf(reason, reason_cap, "no answer through the server");
        return ms;
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
        if (server->proto == VL_PROTO_HYSTERIA2) {
            if (reason && reason_cap)
                snprintf(reason, reason_cap, "profile uses an unsupported transport or security mode");
            return -1;
        }
        return diagnose_server(d, server, 6000, 0, reason, reason_cap);
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

/* the tunnel reads the routing profile's rules and the owner lookup until it is
   stopped, so the previous run is joined before they are rebuilt */
static int awg_begin(daemon_ctl_t *d, const char *path, char *why, size_t why_cap) {
    awg_backend_stop(&d->awg, 0);
    release_routing(d);
    prepare_routing(d);
    int started = awg_backend_start(&d->awg, path, d->settings.dns_upstream, d->rules,
                                    d->owner ? app_owner_policy_lookup : NULL, d->owner,
                                    why, why_cap);
    if (started != 0) release_routing(d);
    return started;
}

int daemon_ctl_awg_busy(void *ctx) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    return d ? awg_backend_busy(&d->awg) : 0;
}

int daemon_ctl_awg(void *ctx, const char *flag, const char *path, char *out, size_t cap) {
    daemon_ctl_t *d = (daemon_ctl_t *)ctx;
    if (out && cap) out[0] = '\0';
    if (!d || !flag || !out || !cap) return 1;
    if (strcmp(flag, "--awg-probe") == 0) return DCTL_AWG_CHILD;
    if (strcmp(flag, "--awg-status") == 0) {
        awg_backend_status(&d->awg, out, cap);
        /* the app reads the uptime from its own line, so the status word
           stays exactly "connected" for every caller that compares it */
        long up = awg_backend_uptime(&d->awg);
        size_t used = strlen(out);
        if (up >= 0 && used < cap)
            snprintf(out + used, cap - used, "\nAWGUP %ld", up);
        return 0;
    }
    if (strcmp(flag, "--awg-stop") == 0) {
        awg_backend_stop(&d->awg, 1);
        release_routing(d);
        snprintf(out, cap, "idle");
        return 0;
    }
    /* the network changed under a running tunnel: its server pin points at
       the old gateway, so the tunnel is rebuilt on the new one */
    char current[256];
    if (strcmp(flag, "--awg-restart") == 0) {
        pthread_mutex_lock(&d->awg.lock);
        snprintf(current, sizeof current, "%s", d->awg.config_path);
        pthread_mutex_unlock(&d->awg.lock);
        if (!awg_backend_busy(&d->awg) || !current[0]) {
            awg_backend_status(&d->awg, out, cap);
            return 0;
        }
        path = current;
        flag = "--awg";
    }
    if (!path || !path[0]) {
        snprintf(out, cap, "error no config path given");
        return 1;
    }
    char why[224];
    if (strcmp(flag, "--awg-validate") == 0) {
        awg_config_t cfg;
        int ok = awg_config_path_ok(path) &&
                 awg_config_load_file(path, &cfg, why, sizeof why) == AWG_CFG_OK;
        if (ok) snprintf(out, cap, "VALID AmneziaWG native config");
        else if (!awg_config_path_ok(path))
            snprintf(out, cap, "ERR the config is not a .conf file in %s", AWG_CONFIG_DIR);
        else snprintf(out, cap, "ERR config rejected: %s", why);
        OPENSSL_cleanse(&cfg, sizeof cfg);
        return ok ? 0 : 1;
    }
    if (strcmp(flag, "--awg") == 0) {
        if (awg_begin(d, path, why, sizeof why) != 0) {
            fprintf(stderr, "senkod: amneziawg not started: %s\n", why);
            snprintf(out, cap, "error %s", why);
            return 1;
        }
        snprintf(out, cap, "connecting");
        return 0;
    }
    snprintf(out, cap, "error unknown amneziawg request %s", flag);
    return 1;
}

int daemon_ctl_awg_restore(daemon_ctl_t *d) {
    char path[256];
    if (!d || !awg_backend_saved_profile(path, sizeof path)) return 0;
    char why[224];
    if (awg_begin(d, path, why, sizeof why) != 0) {
        fprintf(stderr, "senkod: the amneziawg profile left running could not restart: %s\n", why);
        return 0;
    }
    fprintf(stderr, "senkod: bringing back the amneziawg profile %s\n", path);
    return 1;
}
