#define _DEFAULT_SOURCE

#include "utun_backend.h"

#include "awg_route.h"
#include "core/transport_pick.h"
#include "core/senko_time.h"
#include "../common/senko_paths.h"
#include "core/vless.h"
#include "legacy_ios.h"
#include "route_socket.h"
#include "utun_route.h"
#include "vpn_conflict.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <errno.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

/* /var/tmp is emptied at boot, and so are the routes it describes */
#define UTUN_BACKEND_LEFTOVERS "/var/tmp/senkod-utun-routes"
#define UTUN_BACKEND_LEFTOVERS_MAX 16384

static void reason_set(char *out, size_t cap, const char *why) {
    if (out && cap) snprintf(out, cap, "%s", why);
}

static int endpoints_parse(const char *text, tun_plan_input_t *input,
                           uint8_t addresses[][16], uint8_t *lengths) {
    const char *p = text;
    while (p && *p) {
        while (*p == ' ' || *p == ',') ++p;
        if (!*p) break;
        const char *end = strchr(p, ',');
        if (!end) end = p + strlen(p);
        while (end > p && end[-1] == ' ') --end;
        char literal[INET_ADDRSTRLEN];
        size_t len = (size_t)(end - p);
        if (!len || len >= sizeof literal ||
            input->endpoint_count >= TUN_PLAN_MAX_ENDPOINTS) return -1;
        memcpy(literal, p, len);
        literal[len] = '\0';
        size_t index = input->endpoint_count;
        if (inet_pton(AF_INET, literal, addresses[index]) != 1) return -1;
        lengths[index] = 4;
        input->endpoints[index].address_len = 4;
        memcpy(input->endpoints[index].address, addresses[index], 4);
        ++input->endpoint_count;
        p = *end ? end + 1 : end;
    }
    return input->endpoint_count ? 0 : -1;
}

static int physical_path(const tun_plan_input_t *input, utun_route_path_t *out,
                         char *reason, size_t reason_cap) {
    utun_route_error_t error;
    for (size_t i = 0; i < input->endpoint_count; ++i) {
        utun_route_path_t path;
        if (utun_route_system_path(input->endpoints[i].address, 4,
                                   &path, &error) != 0) {
            reason_set(reason, reason_cap, error.message);
            return -1;
        }
        if (i && (strcmp(out->ifname, path.ifname) != 0 ||
                  out->via_gateway != path.via_gateway ||
                  (path.via_gateway && memcmp(out->gateway, path.gateway, 4) != 0))) {
            reason_set(reason, reason_cap,
                "resolved server addresses use different physical routes");
            return -1;
        }
        if (!i) *out = path;
    }
    return 0;
}

int utun_plan_prepare(const char *resolved_ipv4, tun_plan_input_t *input,
                      uint8_t addresses[][16], uint8_t *lengths,
                      char *reason, size_t reason_cap) {
    memset(input, 0, sizeof *input);
    if (endpoints_parse(resolved_ipv4, input, addresses, lengths) != 0) {
        reason_set(reason, reason_cap, "server IPv4 endpoints could not be parsed");
        return -1;
    }
    utun_route_path_t path;
    if (physical_path(input, &path, reason, reason_cap) != 0) {
        vpn_conflict_explain(reason, reason_cap, NULL);
        return -1;
    }
    snprintf(input->physical_ifname, sizeof input->physical_ifname, "%s", path.ifname);
    input->have_gateway4 = path.via_gateway;
    input->route_ipv4 = 1;
    if (path.via_gateway) memcpy(input->gateway4, path.gateway, 4);
    input->enable_ipv6 = 1;
    static const uint8_t public6[16] = {
        0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0x88, 0x88
    };
    utun_route_path_t path6;
    utun_route_error_t route_error;
    /* an ipv4-only carrier can still carry ipv6 app packets through the tunnel */
    if (utun_route_system_path(public6, 16, &path6, &route_error) == 0) {
        if (strcmp(path6.ifname, input->physical_ifname) != 0) {
            reason_set(reason, reason_cap,
                "IPv4 and IPv6 leave through different physical interfaces");
            return -1;
        }
        input->have_gateway6 = path6.via_gateway;
        if (path6.via_gateway) memcpy(input->gateway6, path6.gateway, 16);
    }
    return 0;
}

static int local_address_conflict(void) {
    struct ifaddrs *interfaces = NULL;
    if (getifaddrs(&interfaces) != 0) return -1;
    static const uint8_t private4[][4] = {
        { 198, 18, 0, 1 }, { 198, 18, 0, 2 }
    };
    uint8_t private6[2][16] = {{0}};
    private6[0][0] = private6[1][0] = 0xfd;
    private6[0][2] = private6[1][2] = 0x5e;
    private6[0][3] = private6[1][3] = 0x4b;
    private6[0][15] = 1;
    private6[1][15] = 2;
    int conflict = 0;
    for (struct ifaddrs *item = interfaces; item; item = item->ifa_next) {
        if (!item->ifa_addr) continue;
        if (item->ifa_addr->sa_family == AF_INET) {
            const struct sockaddr_in *address = (const struct sockaddr_in *)item->ifa_addr;
            for (size_t i = 0; i < 2; ++i)
                if (memcmp(&address->sin_addr, private4[i], 4) == 0) conflict = 1;
        } else if (item->ifa_addr->sa_family == AF_INET6) {
            const struct sockaddr_in6 *address = (const struct sockaddr_in6 *)item->ifa_addr;
            for (size_t i = 0; i < 2; ++i)
                if (memcmp(&address->sin6_addr, private6[i], 16) == 0) conflict = 1;
        }
    }
    freeifaddrs(interfaces);
    return conflict;
}

/* a killed daemon leaves its server pins on the physical gateway; after a
   network change they would send that server's traffic nowhere */
void utun_leftovers_save(const tun_plan_t *plan) {
    char text[UTUN_BACKEND_LEFTOVERS_MAX];
    int len = tun_plan_save_leftovers(plan, text, sizeof text);
    const char *tmp = UTUN_BACKEND_LEFTOVERS ".new";
    int fd = len > 0 ? open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600) : -1;
    int ok = fd >= 0 && write(fd, text, (size_t)len) == (ssize_t)len;
    if (fd >= 0 && close(fd) != 0) ok = 0;
    if (ok && rename(tmp, UTUN_BACKEND_LEFTOVERS) == 0) return;
    int saved = errno;
    (void)unlink(tmp);
    fprintf(stderr, "senkod: cannot record the server routes for crash cleanup in %s"
            " (errno %d %s)\n", UTUN_BACKEND_LEFTOVERS, saved, strerror(saved));
}

void utun_leftovers_forget(void) {
    if (unlink(UTUN_BACKEND_LEFTOVERS) != 0 && errno != ENOENT)
        fprintf(stderr, "senkod: cannot remove %s (errno %d %s)\n",
                UTUN_BACKEND_LEFTOVERS, errno, strerror(errno));
}

static void leftovers_undo(void) {
    int fd = open(UTUN_BACKEND_LEFTOVERS, O_RDONLY);
    if (fd < 0) return;
    char text[UTUN_BACKEND_LEFTOVERS_MAX];
    ssize_t len = read(fd, text, sizeof text);
    close(fd);
    tun_plan_t plan;
    if (len <= 0 || (size_t)len >= sizeof text ||
        tun_plan_load_leftovers(text, (size_t)len, &plan) != TUN_PLAN_OK) {
        fprintf(stderr, "senkod: ignoring an unreadable %s from a previous run\n",
                UTUN_BACKEND_LEFTOVERS);
        utun_leftovers_forget();
        return;
    }
    utun_route_result_t result;
    if (utun_route_revert(&plan, utun_route_system_executor(), &result) != UTUN_ROUTE_OK) {
        fprintf(stderr, "senkod: %zu route(s) left by a previous run could not be removed: %s\n",
                result.rollback_failures, result.rollback_message);
        return;
    }
    fprintf(stderr, "senkod: removed %zu route(s) left by a previous run\n",
            result.rolled_back);
    utun_leftovers_forget();
}

#define UTUN_CMD_SNAPSHOT     (TUN_LOOP_CMD_OWNER + 3)
#define UTUN_CMD_FLUSH_DNS    (TUN_LOOP_CMD_OWNER + 4)
#define UTUN_CMD_FLUSH_DIRECT (TUN_LOOP_CMD_OWNER + 5)

static void take_snapshot(utun_backend_t *backend, tun_stack_t *stack) {
    utun_backend_snapshot_t snap;
    memset(&snap, 0, sizeof snap);
    snap.tcp = *tun_tcp_stats(backend->tcp);
    snap.udp = *tun_udp_stats(backend->udp);
    snap.policy = backend->policy.stats;
    snap.stack = *tun_stack_stats(stack);
    const tun_nat_t *nat = tun_stack_capture(stack);
    if (nat) memcpy(snap.nat_dropped, nat->dropped, sizeof snap.nat_dropped);
    if (backend->policy.cache) {
        snap.dns_cache_hits = backend->policy.cache->hits;
        snap.dns_cache_misses = backend->policy.cache->misses;
        snap.dns_cache_stale = backend->policy.cache->stale_hits;
        snap.dns_cache_entries = dns_cache_entry_count(backend->policy.cache);
    }
    if (backend->policy.map) {
        snap.direct_map_enabled = 1;
        dns_policy_counts(backend->policy.map, (uint64_t)(senko_now_ms() / 1000),
                          &snap.direct_map);
        snap.direct_map_evicted = backend->policy.map->evicted;
    }
    pthread_mutex_lock(&backend->lock);
    backend->snapshot = snap;
    ++backend->snapshot_seq;
    pthread_cond_broadcast(&backend->changed);
    pthread_mutex_unlock(&backend->lock);
}

/* runs on the tunnel thread after the udp and tcp relays had their turn */
static void backend_command(void *ctx, tun_stack_t *stack, const tun_loop_command_t *command) {
    utun_backend_t *backend = ctx;
    if (backend->previous_command)
        backend->previous_command(backend->previous_command_ctx, stack, command);
    if (command->type == UTUN_CMD_SNAPSHOT) {
        take_snapshot(backend, stack);
    } else if (command->type == UTUN_CMD_FLUSH_DNS) {
        tun_policy_flush_cache(&backend->policy);
        fprintf(stderr, "senkod: dns cache flushed on request\n");
    } else if (command->type == UTUN_CMD_FLUSH_DIRECT) {
        tun_policy_flush_map(&backend->policy);
        fprintf(stderr, "senkod: direct address table flushed on request\n");
    }
}

static int post(utun_backend_t *backend, int type) {
    if (!backend || !backend->initialized || !backend->active || !backend->loop) return -1;
    tun_loop_command_t command;
    command.type = type;
    command.generation = backend->generation;
    command.argument = 0;
    return tun_loop_post(backend->loop, &command) == TUN_LOOP_OK ? 0 : -1;
}

int utun_backend_snapshot(utun_backend_t *backend, utun_backend_snapshot_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    if (!utun_backend_running(backend)) return -1;
    pthread_mutex_lock(&backend->lock);
    uint64_t wanted = backend->snapshot_seq + 1;
    pthread_mutex_unlock(&backend->lock);
    if (post(backend, UTUN_CMD_SNAPSHOT) != 0) return -1;
    struct timeval now;
    gettimeofday(&now, NULL);
    struct timespec deadline;
    deadline.tv_sec = now.tv_sec + 1;
    deadline.tv_nsec = (long)now.tv_usec * 1000L;
    pthread_mutex_lock(&backend->lock);
    while (backend->snapshot_seq < wanted && !backend->ended)
        if (pthread_cond_timedwait(&backend->changed, &backend->lock, &deadline) == ETIMEDOUT)
            break;
    int ok = backend->snapshot_seq >= wanted;
    if (ok) *out = backend->snapshot;
    pthread_mutex_unlock(&backend->lock);
    return ok ? 0 : -1;
}

int utun_backend_flush_dns(utun_backend_t *backend) {
    return post(backend, UTUN_CMD_FLUSH_DNS);
}

int utun_backend_flush_direct(utun_backend_t *backend) {
    return post(backend, UTUN_CMD_FLUSH_DIRECT);
}

int utun_backend_render_routes(utun_backend_t *backend, char *buf, size_t cap,
                               size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!backend || !backend->active || !buf || !cap) return -1;
    int n = tun_plan_render(&backend->plan, buf, cap);
    if (n < 0) return -1;
    if (out_len) *out_len = (size_t)n < cap ? (size_t)n : cap - 1;
    return 0;
}

static void ready(void *ctx) {
    utun_backend_t *backend = ctx;
    pthread_mutex_lock(&backend->lock);
    backend->ready = 1;
    pthread_cond_broadcast(&backend->changed);
    pthread_mutex_unlock(&backend->lock);
}

static void *run(void *ctx) {
    utun_backend_t *backend = ctx;
    tun_loop_result_t result;
    tun_loop_status_t status = tun_loop_run(backend->loop, &result);
    pthread_mutex_lock(&backend->lock);
    backend->result = result;
    backend->ended = 1;
    if (status != TUN_LOOP_OK)
        snprintf(backend->error, sizeof backend->error, "%s", result.message);
    pthread_cond_broadcast(&backend->changed);
    pthread_mutex_unlock(&backend->lock);
    ssize_t wrote;
    do {
        wrote = write(backend->ended_pipe[1], "e", 1);
    } while (wrote < 0 && errno == EINTR);
    /* a full pipe already holds the wakeup */
    return NULL;
}

static int open_ended_pipe(int fds[2]) {
    if (pipe(fds) != 0) return -1;
    for (int i = 0; i < 2; ++i) {
        int fl = fcntl(fds[i], F_GETFL, 0);
        if (fl < 0 || fcntl(fds[i], F_SETFL, fl | O_NONBLOCK) != 0 ||
            fcntl(fds[i], F_SETFD, FD_CLOEXEC) != 0) {
            close(fds[0]);
            close(fds[1]);
            fds[0] = fds[1] = -1;
            return -1;
        }
    }
    return 0;
}

int utun_backend_ended_fd(const utun_backend_t *backend) {
    return backend && backend->initialized ? backend->ended_pipe[0] : -1;
}

void utun_backend_drain_ended(utun_backend_t *backend) {
    if (!backend || !backend->initialized) return;
    char sink[16];
    ssize_t got;
    do {
        got = read(backend->ended_pipe[0], sink, sizeof sink);
    } while (got > 0 || (got < 0 && errno == EINTR));
}

int utun_backend_runtime_init(utun_backend_t *backend) {
    if (!backend) return -1;
    memset(backend, 0, sizeof *backend);
    backend->ended_pipe[0] = backend->ended_pipe[1] = -1;
    utun_device_init(&backend->device);
    utun_dns_init(&backend->dns);
    if (open_ended_pipe(backend->ended_pipe) != 0) return -1;
    if (pthread_mutex_init(&backend->lock, NULL) != 0) {
        close(backend->ended_pipe[0]);
        close(backend->ended_pipe[1]);
        backend->ended_pipe[0] = backend->ended_pipe[1] = -1;
        return -1;
    }
    if (pthread_cond_init(&backend->changed, NULL) != 0) {
        pthread_mutex_destroy(&backend->lock);
        close(backend->ended_pipe[0]);
        close(backend->ended_pipe[1]);
        backend->ended_pipe[0] = backend->ended_pipe[1] = -1;
        return -1;
    }
    backend->initialized = 1;
    leftovers_undo();
    return 0;
}

void utun_backend_stop(utun_backend_t *backend) {
    if (!backend || !backend->initialized) return;
    if (backend->dialer.ready) tun_dialer_cancel(&backend->dialer);
    hy2_engine_cancel(backend->hy2);
    utun_dns_withdraw(&backend->dns);
    backend->dns_state[0] = '\0';
    if (backend->thread_started) {
        tun_loop_request_stop(backend->loop);
        pthread_join(backend->thread, NULL);
        backend->thread_started = 0;
    }
    if (backend->active) {
        utun_route_result_t result;
        if (utun_route_revert(&backend->plan, utun_route_system_executor(),
                              &result) != UTUN_ROUTE_OK)
            /* the record stays, so the next start tries the pins again */
            fprintf(stderr, "senkod: utun route cleanup failed: %s\n",
                    result.rollback_message[0] ? result.rollback_message
                                               : result.failed_message);
        else
            utun_leftovers_forget();
        backend->active = 0;
    }
    /* a zeroed loop that never ran tun_loop_init reads its wake pipe as fd 0
       and has no device, so it must not be torn down as a loop */
    if (backend->loop_initialized) tun_loop_destroy(backend->loop);
    utun_device_close(&backend->device);
    backend->loop_initialized = 0;
    free(backend->loop);
    free(backend->tcp);
    free(backend->udp);
    if (backend->policy_ready) tun_policy_free(&backend->policy);
    backend->policy_ready = 0;
    backend->loop = NULL;
    backend->tcp = NULL;
    backend->udp = NULL;
    if (backend->dialer.ready) tun_dialer_destroy(&backend->dialer);
    /* the opener threads were joined with the loop, so no dial is still inside */
    hy2_engine_stop(backend->hy2);
    backend->hy2 = NULL;
    backend->ready = 0;
    backend->ended = 0;
}

void utun_backend_destroy(utun_backend_t *backend) {
    if (!backend || !backend->initialized) return;
    utun_backend_stop(backend);
    pthread_cond_destroy(&backend->changed);
    pthread_mutex_destroy(&backend->lock);
    close(backend->ended_pipe[0]);
    close(backend->ended_pipe[1]);
    backend->ended_pipe[0] = backend->ended_pipe[1] = -1;
    backend->initialized = 0;
}

int utun_backend_running(utun_backend_t *backend) {
    if (!backend || !backend->initialized || !backend->active) return 0;
    pthread_mutex_lock(&backend->lock);
    int running = backend->ready && !backend->ended;
    pthread_mutex_unlock(&backend->lock);
    return running;
}

void utun_backend_stats(utun_backend_t *backend, uint64_t *up, uint64_t *down) {
    if (up) *up = 0;
    if (down) *down = 0;
    if (!backend || !backend->initialized || !backend->active) return;
    tun_loop_byte_counts(backend->loop, up, down);
}

/* the hysteria2 client runs inside senkod because senko-core's go runtime
   needs ios 12 on arm64 and has no armv7 build at all */
static int hysteria_engine_start(utun_backend_t *backend, const vl_server_t *server,
                                 const uint8_t addresses[][16], const uint8_t *lengths,
                                 size_t count) {
    if (server->obfs[0] && strcmp(server->obfs, "none") != 0 &&
        (strcmp(server->obfs, "salamander") != 0 || !server->obfs_password[0])) {
        snprintf(backend->error, sizeof backend->error,
                 "hysteria2 obfuscation \"%.31s\" is not supported, only salamander with a password",
                 server->obfs);
        return -1;
    }
    hy2_engine_config_t config;
    memset(&config, 0, sizeof config);
    config.addresses = addresses;
    config.address_lens = lengths;
    config.address_count = count;
    config.port = server->port;
    config.sni = server->sni;
    config.peer_host = server->host;
    config.auth = server->pass;
    config.pin_sha256 = server->pin_sha256;
    if (server->obfs[0] && strcmp(server->obfs, "none") != 0)
        config.obfs_password = server->obfs_password;
    config.ca_file = SENKO_USR_LIB "/senko/cacert.pem";
    config.connect_timeout_ms = 8000;
    backend->hy2 = hy2_engine_start(&config, backend->error, sizeof backend->error);
    return backend->hy2 ? 0 : -1;
}

void utun_backend_set_app_owner(utun_backend_t *backend, tun_policy_app_fn fn, void *ctx) {
    if (!backend) return;
    backend->app_of = fn;
    backend->app_ctx = ctx;
}

int utun_backend_start(utun_backend_t *backend, const vl_server_t *server,
                       const char *resolved_ipv4, ruleset_t *rules,
                       const char *dns_upstream, dns_block_response_t block_response,
                       char *reason, size_t reason_cap) {
    if (reason && reason_cap) reason[0] = '\0';
    if (!backend || !backend->initialized || !server || !resolved_ipv4 ||
        !dns_upstream) return -1;
    utun_backend_stop(backend);
    backend->error[0] = '\0';
    int hysteria = server->proto == VL_PROTO_HYSTERIA2;
    const transport_vt_t *vt = hysteria ? &transport_hy2 : transport_for_server(server);
    if (!vt) {
        reason_set(reason, reason_cap,
            "this profile's protocol or transport has no senko client to carry it");
        return -1;
    }
    int vless = server->proto == VL_PROTO_VLESS;
    uint8_t uuid[VLESS_UUID_LEN];
    memset(uuid, 0, sizeof uuid);
    if (cfg_proto_uses_uuid(server->proto) && vless_uuid_parse(server->uuid, uuid) != VLESS_OK) {
        reason_set(reason, reason_cap, "the UUID in this profile is invalid");
        return -1;
    }
    uint8_t dns[16] = {0};
    if (inet_pton(AF_INET, dns_upstream, dns) != 1) {
        reason_set(reason, reason_cap, "DNS upstream must be a numeric IPv4 address");
        return -1;
    }
    tun_plan_input_t input;
    uint8_t addresses[TUN_PLAN_MAX_ENDPOINTS][16] = {{0}};
    uint8_t lengths[TUN_PLAN_MAX_ENDPOINTS] = {0};
    if (utun_plan_prepare(resolved_ipv4, &input, addresses, lengths,
                          reason, reason_cap) != 0)
        return -1;
    int address_conflict = local_address_conflict();
    if (address_conflict != 0) {
        reason_set(reason, reason_cap, address_conflict > 0
            ? "the selected utun address is already in use on another interface"
            : "device interface addresses could not be inspected");
        return -1;
    }
    memset(&backend->direct, 0, sizeof backend->direct);
    snprintf(backend->direct.ifname, sizeof backend->direct.ifname, "%s",
             input.physical_ifname);
    backend->direct.ifindex = if_nametoindex(input.physical_ifname);

    backend->server = *server;
    if (utun_device_open(&backend->device, 0) != UTUN_DEVICE_OK) {
        snprintf(backend->error, sizeof backend->error, "cannot open a utun device");
        route_errno_append(backend->error, sizeof backend->error,
                           backend->device.last_errno);
        goto failed;
    }
    snprintf(input.ifname, sizeof input.ifname, "%s", backend->device.ifname);
    if (tun_plan_build(&input, &backend->plan) != TUN_PLAN_OK) {
        reason_set(backend->error, sizeof backend->error, "utun route plan is invalid");
        goto failed;
    }

    awg_route_plan_t interface;
    memset(&interface, 0, sizeof interface);
    snprintf(interface.ifname, sizeof interface.ifname, "%s", input.ifname);
    snprintf(interface.ipv4, sizeof interface.ipv4, "198.18.0.1");
    snprintf(interface.peer4, sizeof interface.peer4, "198.18.0.2");
    snprintf(interface.ipv6, sizeof interface.ipv6, "fd00:5e4b::1");
    interface.has_ipv4 = interface.has_ipv6 = 1;
    interface.mtu = 1500;
    if (awg_route_interface_up(&interface) != 0) {
        int saved = errno;
        snprintf(backend->error, sizeof backend->error,
                 "cannot give %s its tunnel address", input.ifname);
        route_errno_append(backend->error, sizeof backend->error, saved);
        goto failed;
    }
    tun_opener_dial_fn dial = tun_dialer_connect;
    void *dial_ctx = &backend->dialer;
    if (hysteria) {
        if (hysteria_engine_start(backend, server, (const uint8_t (*)[16])addresses, lengths,
                                  input.endpoint_count) != 0)
            goto failed;
        dial = hy2_engine_dial;
        dial_ctx = backend->hy2;
    } else if (tun_dialer_init(&backend->dialer, addresses, lengths,
                               input.endpoint_count, server->port, 8000) != 0) {
        reason_set(backend->error, sizeof backend->error, "server dialer could not start");
        goto failed;
    }
    tun_policy_status_t policy_status = tun_policy_init(&backend->policy, rules);
    if (policy_status != TUN_POLICY_OK) {
        reason_set(backend->error, sizeof backend->error,
                   "not enough memory for the dns cache and rule tables");
        goto failed;
    }
    backend->policy_ready = 1;
    backend->policy.app_of = backend->app_of;
    backend->policy.app_ctx = backend->app_ctx;
    backend->loop = calloc(1, tun_loop_size());
    backend->tcp = calloc(1, tun_tcp_size());
    backend->udp = calloc(1, tun_udp_size());
    if (!backend->loop || !backend->tcp || !backend->udp) {
        reason_set(backend->error, sizeof backend->error, "not enough memory for utun flows");
        goto failed;
    }
    tun_loop_config_t config;
    memset(&config, 0, sizeof config);
    config.generation = ++backend->generation;
    config.stack.mtu = 1500;
    config.stack.have_ipv4 = config.stack.have_ipv6 = 1;
    config.stack.address4[0] = 198;
    config.stack.address4[1] = 18;
    config.stack.address4[3] = 1;
    memset(config.stack.netmask4, 255, 4);
    config.stack.synthetic4[0] = 198;
    config.stack.synthetic4[1] = 18;
    config.stack.synthetic4[3] = 2;
    config.stack.address6[0] = config.stack.synthetic6[0] = 0xfd;
    config.stack.address6[2] = config.stack.synthetic6[2] = 0x5e;
    config.stack.address6[3] = config.stack.synthetic6[3] = 0x4b;
    config.stack.address6[15] = 1;
    config.stack.synthetic6[15] = 2;
    snprintf(config.stack.ifname, sizeof config.stack.ifname, "%s", input.ifname);
    config.on_ready = ready;
    config.ready_ctx = backend;
    config.retain_device_until_destroy = 1;

    transport_tls_cfg_t tls;
    memset(&tls, 0, sizeof tls);
    tls.sni = backend->server.sni;
    tls.fingerprint = backend->server.fp;
    tls.reality_pbk = backend->server.pbk;
    tls.reality_sid = backend->server.sid;
    tls.path = backend->server.path;
    tls.ws_host = backend->server.ws_host;
    tls.xhttp_mode = backend->server.mode;
    tls.peer_host = backend->server.host;
    tls.insecure = backend->server.insecure;
    tun_tcp_config_t tcp;
    memset(&tcp, 0, sizeof tcp);
    tcp.vt = vt;
    tcp.tls = tls;
    tcp.proto = server->proto;
    memcpy(tcp.uuid, uuid, sizeof uuid);
    tcp.flow = vless ? backend->server.flow : NULL;
    tcp.user = backend->server.user;
    tcp.pass = backend->server.pass;
    tcp.policy = &backend->policy;
    tcp.direct_socket = direct_socket_bound;
    tcp.direct_ctx = &backend->direct;
    tcp.dial = dial;
    tcp.dial_ctx = dial_ctx;
    tcp.opener_threads = 2;
    tcp.max_flows = 8;
    tcp.use_shared_tls = !hysteria && server->security != VL_SEC_NONE;
    memcpy(tcp.dns_upstream, dns, 4);
    tcp.dns_upstream_len = 4;
    tun_udp_config_t udp;
    memset(&udp, 0, sizeof udp);
    udp.vt = vt;
    udp.tls = tls;
    udp.proto = server->proto;
    memcpy(udp.uuid, uuid, sizeof uuid);
    udp.flow = vless ? backend->server.flow : NULL;
    udp.user = backend->server.user;
    udp.pass = backend->server.pass;
    udp.policy = &backend->policy;
    udp.direct_socket = direct_socket_bound;
    udp.direct_ctx = &backend->direct;
    udp.dial = dial;
    udp.dial_ctx = dial_ctx;
    udp.opener_threads = 1;
    udp.block_response = block_response;
    memcpy(udp.dns_upstream, dns, 4);
    udp.dns_upstream_len = 4;
    udp.use_shared_tls = !hysteria && server->security != VL_SEC_NONE;
    int relays_ok = tun_tcp_init(backend->tcp, &tcp, &config) == TUN_TCP_OK &&
                    tun_udp_init(backend->udp, &udp, &config) == 0;
    if (relays_ok) {
        backend->previous_command = config.on_command;
        backend->previous_command_ctx = config.command_ctx;
        config.on_command = backend_command;
        config.command_ctx = backend;
    }
    if (!relays_ok ||
        tun_loop_init(backend->loop, &config, &backend->device) != TUN_LOOP_OK) {
        reason_set(backend->error, sizeof backend->error,
                   "utun packet loop could not be initialized");
        goto failed;
    }
    backend->loop_initialized = 1;
    tun_tcp_bind(backend->tcp, backend->loop, config.generation);
    tun_udp_bind(backend->udp, backend->loop, config.generation);
    if (pthread_create(&backend->thread, NULL, run, backend) != 0) {
        reason_set(backend->error, sizeof backend->error,
                   "utun packet thread could not be created");
        goto failed;
    }
    backend->thread_started = 1;
    struct timespec deadline;
    struct timeval now;
    gettimeofday(&now, NULL);
    deadline.tv_sec = now.tv_sec + 8;
    deadline.tv_nsec = (long)now.tv_usec * 1000L;
    pthread_mutex_lock(&backend->lock);
    while (!backend->ready && !backend->ended) {
        int waited = pthread_cond_timedwait(&backend->changed, &backend->lock, &deadline);
        if (waited == ETIMEDOUT) break;
    }
    int started = backend->ready && !backend->ended;
    pthread_mutex_unlock(&backend->lock);
    if (!started) {
        snprintf(backend->error, sizeof backend->error, "%s",
                 backend->result.message[0] ? backend->result.message
                                            : "utun packet loop did not become ready");
        goto failed;
    }
    utun_route_result_t result;
    if (utun_route_apply_verified(&backend->plan, utun_route_system_executor(),
                                  &result) != UTUN_ROUTE_OK) {
        snprintf(backend->error, sizeof backend->error, "%.255s",
                 result.failed_message[0] ? result.failed_message
                                          : "utun route installation failed");
        vpn_conflict_explain(backend->error, sizeof backend->error, input.ifname);
        goto failed;
    }
    utun_leftovers_save(&backend->plan);
    backend->active = 1;
    char dns_reason[160];
    if (utun_dns_publish(&backend->dns, input.ifname, "198.18.0.1", "198.18.0.2",
                         dns_upstream, dns_reason, sizeof dns_reason) == 0) {
        snprintf(backend->dns_state, sizeof backend->dns_state,
                 "system dns is %s through %s", dns_upstream, input.ifname);
    } else {
        snprintf(backend->dns_state, sizeof backend->dns_state,
                 "system dns stays outside the tunnel: %.150s", dns_reason);
        fprintf(stderr, "senkod: %s\n", backend->dns_state);
        if (senko_ios_major() >= 12) {
            reason_set(backend->error, sizeof backend->error, backend->dns_state);
            goto failed;
        }
    }
    return 0;
failed:
    reason_set(reason, reason_cap, backend->error);
    utun_backend_stop(backend);
    return -1;
}
