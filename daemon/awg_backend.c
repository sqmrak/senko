#define _DEFAULT_SOURCE

#include "awg_backend.h"

#include "awg_route.h"
#include "core/senko_time.h"
#include "direct_socket.h"
#include "legacy_ios.h"
#include "status.h"
#include "utun_backend.h"
#include "utun_route.h"
#include "vpn_conflict.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/crypto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define AWG_HANDSHAKE_TIMEOUT_MS 5000

int awg_backend_init(awg_backend_t *backend) {
    if (!backend) return -1;
    memset(backend, 0, sizeof *backend);
    backend->wake[0] = backend->wake[1] = -1;
    backend->udp_fd = -1;
    utun_device_init(&backend->device);
    utun_dns_init(&backend->dns);
    if (pthread_mutex_init(&backend->lock, NULL) != 0) return -1;
    backend->initialized = 1;
    return 0;
}

void awg_backend_destroy(awg_backend_t *backend) {
    if (!backend || !backend->initialized) return;
    awg_backend_stop(backend, 0);
    pthread_mutex_destroy(&backend->lock);
    backend->initialized = 0;
}

static void set_detail(awg_backend_t *backend, awg_backend_state_t state, const char *why) {
    pthread_mutex_lock(&backend->lock);
    backend->state = state;
    snprintf(backend->detail, sizeof backend->detail, "%s", why ? why : "");
    pthread_mutex_unlock(&backend->lock);
}

static int resolve_endpoint(const awg_config_t *cfg, char *out, size_t cap,
                            char *why, size_t why_cap) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    int rc = getaddrinfo(cfg->endpoint_host, NULL, &hints, &res);
    if (rc != 0 || !res) {
        snprintf(why, why_cap, "cannot resolve the server %.100s: %s",
                 cfg->endpoint_host, gai_strerror(rc));
        return -1;
    }
    const struct sockaddr_in *sin = (const struct sockaddr_in *)res->ai_addr;
    int ok = inet_ntop(AF_INET, &sin->sin_addr, out, (socklen_t)cap) != NULL;
    freeaddrinfo(res);
    if (!ok) snprintf(why, why_cap, "the server address does not fit");
    return ok ? 0 : -1;
}

static const char *first_ipv4_dns(const awg_config_t *cfg, const char *fallback) {
    for (size_t i = 0; i < cfg->dns_count; ++i) {
        struct in_addr a;
        if (inet_pton(AF_INET, cfg->dns[i], &a) == 1) return cfg->dns[i];
    }
    return fallback;
}

static void *run(void *arg) {
    awg_backend_t *b = arg;
    char why[224];
    why[0] = '\0';
    int stopped = 0;
    int status_on = 0;
    awg_link_t *link = NULL;
    awg_handshake_t hs;
    memset(&hs, 0, sizeof hs);

    char endpoint[64];
    tun_plan_input_t input;
    uint8_t addresses[TUN_PLAN_MAX_ENDPOINTS][16];
    uint8_t lengths[TUN_PLAN_MAX_ENDPOINTS];
    if (resolve_endpoint(&b->cfg, endpoint, sizeof endpoint, why, sizeof why) != 0 ||
        utun_plan_prepare(endpoint, &input, addresses, lengths, why, sizeof why) != 0)
        goto done;
    pthread_mutex_lock(&b->lock);
    snprintf(b->endpoint, sizeof b->endpoint, "%s", endpoint);
    snprintf(b->physical_ifname, sizeof b->physical_ifname, "%s", input.physical_ifname);
    pthread_mutex_unlock(&b->lock);

    /* bound to the physical interface, so the server keeps being reached
       outside the tunnel whatever the routing table says */
    direct_socket_iface_t iface;
    memset(&iface, 0, sizeof iface);
    snprintf(iface.ifname, sizeof iface.ifname, "%s", input.physical_ifname);
    iface.ifindex = if_nametoindex(input.physical_ifname);
    b->udp_fd = direct_socket_bound(&iface, AF_INET, SOCK_DGRAM, why, sizeof why);
    if (b->udp_fd < 0) goto done;
    struct sockaddr_in server;
    memset(&server, 0, sizeof server);
    server.sin_family = AF_INET;
    server.sin_port = htons(b->cfg.endpoint_port);
    (void)inet_pton(AF_INET, endpoint, &server.sin_addr);
    if (connect(b->udp_fd, (struct sockaddr *)&server, sizeof server) != 0) {
        snprintf(why, sizeof why, "cannot reach %s:%u over %s (errno %d)", endpoint,
                 (unsigned)b->cfg.endpoint_port, input.physical_ifname, errno);
        goto done;
    }

    char hs_reason[160];
    hs_reason[0] = '\0';
    awg_hs_status_t hr = awg_handshake_establish_fd(b->udp_fd, b->wake[0], &b->cfg,
                                                    AWG_HANDSHAKE_TIMEOUT_MS, &hs,
                                                    hs_reason, sizeof hs_reason);
    if (hr == AWG_HS_CANCELLED) {
        stopped = 1;
        goto done;
    }
    if (hr != AWG_HS_OK) {
        snprintf(why, sizeof why, "handshake with %s:%u failed: %s", endpoint,
                 (unsigned)b->cfg.endpoint_port, hs_reason);
        goto done;
    }

    if (utun_device_open(&b->device, 0) != UTUN_DEVICE_OK) {
        snprintf(why, sizeof why, "cannot open a utun device (errno %d)",
                 b->device.last_errno);
        goto done;
    }
    awg_route_plan_t address;
    if (awg_route_plan_for_interface(&b->cfg, b->device.ifname, &address) != 0 ||
        awg_route_interface_up(&address) != 0) {
        snprintf(why, sizeof why, "cannot give %s the profile's address (errno %d)",
                 b->device.ifname, errno);
        goto done;
    }
    pthread_mutex_lock(&b->lock);
    snprintf(b->ifname, sizeof b->ifname, "%s", b->device.ifname);
    pthread_mutex_unlock(&b->lock);

    snprintf(input.ifname, sizeof input.ifname, "%s", b->device.ifname);
    if (tun_plan_build(&input, &b->plan) != TUN_PLAN_OK) {
        snprintf(why, sizeof why, "the route plan for %s is invalid", b->device.ifname);
        goto done;
    }
    utun_route_result_t routes;
    if (utun_route_apply_verified(&b->plan, utun_route_system_executor(),
                                  &routes) != UTUN_ROUTE_OK) {
        snprintf(why, sizeof why, "%.200s", routes.failed_message[0]
                 ? routes.failed_message : "the tunnel routes could not be installed");
        vpn_conflict_explain(why, sizeof why, b->device.ifname);
        goto done;
    }
    b->routes_active = 1;
    utun_leftovers_save(&b->plan);

    const char *dns = first_ipv4_dns(&b->cfg, b->dns_fallback);
    char dns_reason[160];
    char dns_state[192];
    if (utun_dns_publish(&b->dns, b->device.ifname, address.ipv4, address.ipv4, dns,
                         dns_reason, sizeof dns_reason) == 0) {
        snprintf(dns_state, sizeof dns_state, "system dns is %s through %s", dns,
                 b->device.ifname);
    } else {
        snprintf(dns_state, sizeof dns_state, "system dns stays outside the tunnel: %.150s",
                 dns_reason);
        fprintf(stderr, "senkod: amneziawg %s\n", dns_state);
        if (senko_ios_major() >= 12) {
            snprintf(why, sizeof why, "%s", dns_state);
            goto done;
        }
    }

    link = calloc(1, sizeof *link);
    if (!link) {
        snprintf(why, sizeof why, "not enough memory for the packet buffers");
        goto done;
    }
    awg_link_init(link, &b->cfg, &b->device, b->udp_fd, b->wake[0], &hs, senko_now_ms());
    link->stats_lock = &b->lock;
    pthread_mutex_lock(&b->lock);
    snprintf(b->dns_state, sizeof b->dns_state, "%s", dns_state);
    b->link = link;
    b->state = AWG_BACKEND_CONNECTED;
    b->connected_ms = senko_now_ms();
    pthread_mutex_unlock(&b->lock);
    status_set_on(input.physical_ifname);
    status_on = 1;
    fprintf(stderr, "senkod: amneziawg connected, %s to %s:%u through %s\n",
            b->device.ifname, endpoint, (unsigned)b->cfg.endpoint_port,
            input.physical_ifname);

    if (awg_link_run(link, why, sizeof why) == AWG_LINK_STOPPED) stopped = 1;

done:
    /* dns first: configd hands the primary interface back to wifi before the
       vpn badge goes, so the status bar never shows neither */
    utun_dns_withdraw(&b->dns);
    if (b->routes_active) {
        utun_route_result_t result;
        if (utun_route_revert(&b->plan, utun_route_system_executor(), &result) != UTUN_ROUTE_OK)
            fprintf(stderr, "senkod: amneziawg route cleanup failed: %s\n",
                    result.rollback_message[0] ? result.rollback_message
                                               : result.failed_message);
        else
            utun_leftovers_forget();
        b->routes_active = 0;
    }
    utun_device_close(&b->device);
    if (b->udp_fd >= 0) close(b->udp_fd);
    b->udp_fd = -1;
    if (status_on) status_set(0);
    OPENSSL_cleanse(&hs, sizeof hs);

    pthread_mutex_lock(&b->lock);
    if (link) {
        b->bytes_up += link->bytes_up;
        b->bytes_down += link->bytes_down;
    }
    b->link = NULL;
    b->state = stopped ? AWG_BACKEND_IDLE : AWG_BACKEND_ERROR;
    snprintf(b->detail, sizeof b->detail, "%s", stopped ? "" : why);
    b->dns_state[0] = '\0';
    b->ifname[0] = '\0';
    b->thread_done = 1;
    pthread_mutex_unlock(&b->lock);
    if (link) {
        awg_link_clear(link);
        free(link);
    }
    if (stopped)
        fprintf(stderr, "senkod: amneziawg stopped\n");
    else
        fprintf(stderr, "senkod: amneziawg failed: %s\n", why);
    return NULL;
}

static void save_profile(const char *path) {
    const char *tmp = AWG_BACKEND_ACTIVE_PATH ".new";
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int ok = fd >= 0 && dprintf(fd, "%s\n", path) > 0;
    if (fd >= 0 && close(fd) != 0) ok = 0;
    if (ok && rename(tmp, AWG_BACKEND_ACTIVE_PATH) == 0) return;
    (void)unlink(tmp);
    fprintf(stderr, "senkod: cannot record the running amneziawg profile in %s (errno %d)\n",
            AWG_BACKEND_ACTIVE_PATH, errno);
}

int awg_backend_saved_profile(char *path, size_t cap) {
    FILE *f = fopen(AWG_BACKEND_ACTIVE_PATH, "r");
    if (!f) return 0;
    char line[300];
    int ok = fgets(line, sizeof line, f) != NULL;
    fclose(f);
    if (!ok) return 0;
    line[strcspn(line, "\r\n")] = '\0';
    if (!awg_config_path_ok(line) || strlen(line) >= cap) return 0;
    snprintf(path, cap, "%s", line);
    return 1;
}

int awg_backend_start(awg_backend_t *backend, const char *path, const char *dns_fallback,
                      char *reason, size_t reason_cap) {
    if (reason && reason_cap) reason[0] = '\0';
    if (!backend || !backend->initialized || !path) return -1;
    if (!awg_config_path_ok(path)) {
        if (reason) snprintf(reason, reason_cap, "the config is not a .conf file in "
                             AWG_CONFIG_DIR ": %.120s", path);
        return -1;
    }
    awg_backend_stop(backend, 0);
    char why[128];
    if (awg_config_load_file(path, &backend->cfg, why, sizeof why) != AWG_CFG_OK) {
        if (reason) snprintf(reason, reason_cap, "config rejected: %s", why);
        return -1;
    }
    awg_route_plan_t address;
    if (awg_route_plan_for_interface(&backend->cfg, "utun0", &address) != 0 ||
        !address.has_ipv4) {
        if (reason) snprintf(reason, reason_cap, "the profile has no IPv4 Address");
        return -1;
    }
    if (pipe(backend->wake) != 0) {
        if (reason) snprintf(reason, reason_cap, "cannot create the stop pipe (errno %d)", errno);
        backend->wake[0] = backend->wake[1] = -1;
        return -1;
    }
    for (int i = 0; i < 2; ++i) (void)fcntl(backend->wake[i], F_SETFD, FD_CLOEXEC);
    pthread_mutex_lock(&backend->lock);
    backend->state = AWG_BACKEND_CONNECTING;
    backend->detail[0] = '\0';
    backend->endpoint[0] = backend->physical_ifname[0] = '\0';
    snprintf(backend->config_path, sizeof backend->config_path, "%s", path);
    snprintf(backend->dns_fallback, sizeof backend->dns_fallback, "%s",
             dns_fallback ? dns_fallback : "");
    backend->thread_done = 0;
    pthread_mutex_unlock(&backend->lock);
    save_profile(path);
    if (pthread_create(&backend->thread, NULL, run, backend) != 0) {
        close(backend->wake[0]);
        close(backend->wake[1]);
        backend->wake[0] = backend->wake[1] = -1;
        set_detail(backend, AWG_BACKEND_ERROR, "cannot start the tunnel thread");
        if (reason) snprintf(reason, reason_cap, "cannot start the tunnel thread");
        return -1;
    }
    backend->thread_started = 1;
    fprintf(stderr, "senkod: amneziawg connecting to %s:%u\n", backend->cfg.endpoint_host,
            (unsigned)backend->cfg.endpoint_port);
    return 0;
}

void awg_backend_stop(awg_backend_t *backend, int forget) {
    if (!backend || !backend->initialized) return;
    if (backend->thread_started) {
        char one = 1;
        while (write(backend->wake[1], &one, 1) < 0 && errno == EINTR) {}
        pthread_join(backend->thread, NULL);
        backend->thread_started = 0;
    }
    if (backend->wake[0] >= 0) close(backend->wake[0]);
    if (backend->wake[1] >= 0) close(backend->wake[1]);
    backend->wake[0] = backend->wake[1] = -1;
    OPENSSL_cleanse(&backend->cfg, sizeof backend->cfg);
    if (forget) {
        set_detail(backend, AWG_BACKEND_IDLE, "");
        if (unlink(AWG_BACKEND_ACTIVE_PATH) != 0 && errno != ENOENT)
            fprintf(stderr, "senkod: cannot remove %s (errno %d)\n",
                    AWG_BACKEND_ACTIVE_PATH, errno);
    }
}

void awg_backend_status(awg_backend_t *backend, char *out, size_t cap) {
    if (!out || !cap) return;
    if (!backend || !backend->initialized) {
        snprintf(out, cap, "idle");
        return;
    }
    pthread_mutex_lock(&backend->lock);
    switch (backend->state) {
    case AWG_BACKEND_CONNECTING: snprintf(out, cap, "connecting"); break;
    case AWG_BACKEND_CONNECTED:  snprintf(out, cap, "connected"); break;
    case AWG_BACKEND_ERROR:      snprintf(out, cap, "error %s", backend->detail); break;
    default:                     snprintf(out, cap, "idle"); break;
    }
    pthread_mutex_unlock(&backend->lock);
}

int awg_backend_busy(awg_backend_t *backend) {
    if (!backend || !backend->initialized) return 0;
    pthread_mutex_lock(&backend->lock);
    int busy = backend->state == AWG_BACKEND_CONNECTING ||
               backend->state == AWG_BACKEND_CONNECTED;
    pthread_mutex_unlock(&backend->lock);
    return busy;
}

long awg_backend_uptime(awg_backend_t *backend) {
    if (!backend || !backend->initialized) return -1;
    pthread_mutex_lock(&backend->lock);
    long seconds = -1;
    if (backend->state == AWG_BACKEND_CONNECTED)
        seconds = (long)((senko_now_ms() - backend->connected_ms) / 1000);
    pthread_mutex_unlock(&backend->lock);
    return seconds;
}

void awg_backend_stats(awg_backend_t *backend, uint64_t *up, uint64_t *down) {
    if (up) *up = 0;
    if (down) *down = 0;
    if (!backend || !backend->initialized) return;
    pthread_mutex_lock(&backend->lock);
    uint64_t u = backend->bytes_up, d = backend->bytes_down;
    if (backend->link) {
        u += backend->link->bytes_up;
        d += backend->link->bytes_down;
    }
    pthread_mutex_unlock(&backend->lock);
    if (up) *up = u;
    if (down) *down = d;
}

int awg_backend_describe(awg_backend_t *backend, int index, char *key, size_t key_cap,
                         char *value, size_t value_cap) {
    static const char *keys[] = { "awg.state", "awg.profile", "awg.route", "awg.dns", "awg.link" };
    const int count = (int)(sizeof keys / sizeof keys[0]);
    if (!backend || !backend->initialized || index < 0 || index >= count) return count;
    snprintf(key, key_cap, "%s", keys[index]);
    if (index == 0) {
        awg_backend_status(backend, value, value_cap);
        return count;
    }
    pthread_mutex_lock(&backend->lock);
    switch (index) {
    case 1:
        snprintf(value, value_cap, "%s", backend->config_path[0] ? backend->config_path : "none");
        break;
    case 2:
        if (backend->ifname[0])
            snprintf(value, value_cap, "%s, server %s pinned to %s", backend->ifname,
                     backend->endpoint, backend->physical_ifname);
        else
            snprintf(value, value_cap, "no tunnel");
        break;
    case 3:
        snprintf(value, value_cap, "%s", backend->dns_state[0] ? backend->dns_state : "untouched");
        break;
    default:
        if (backend->link)
            snprintf(value, value_cap, "%u key renewal(s), %u packet(s) dropped",
                     (unsigned)backend->link->renewals, (unsigned)backend->link->dropped);
        else
            snprintf(value, value_cap, "down");
        break;
    }
    pthread_mutex_unlock(&backend->lock);
    return count;
}
