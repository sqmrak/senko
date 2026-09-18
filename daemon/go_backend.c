#define _DEFAULT_SOURCE

#include "go_backend.h"
#include "go_config.h"
#include "awg_utun.h"
#include "awg_pfroute.h"
#include "legacy_ios.h"
#include "../common/senko_paths.h"
#include "core/net_safe.h"

#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

extern char **environ;

#define GO_CORE SENKO_USR_LIB "/senko-core"
#define GO_CORE_CONFIG "/var/run/senko-core.json"
#define GO_CONFIG_MAX (2 * 1024 * 1024)

static void set_reason(char *reason, size_t cap, const char *value) {
    if (reason && cap) snprintf(reason, cap, "%s", value ? value : "go backend failed");
}

int go_backend_supported(void) {
#if defined(__LP64__)
    return senko_ios_major() >= 12;
#else
    return 0;
#endif
}

static int write_config(const char *contents) {
    char temporary[128];
    int n = snprintf(temporary, sizeof temporary, "%s.%ld", GO_CORE_CONFIG, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof temporary) return -1;
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    size_t length = strlen(contents);
    size_t offset = 0;
    while (offset < length) {
        ssize_t wrote = write(fd, contents + offset, length - offset);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) {
            close(fd);
            unlink(temporary);
            return -1;
        }
        offset += (size_t)wrote;
    }
    if (fsync(fd) != 0 || close(fd) != 0 || rename(temporary, GO_CORE_CONFIG) != 0) {
        unlink(temporary);
        return -1;
    }
    return 0;
}

static int spawn_core(go_backend_t *backend) {
    char fd_text[24];
    char *argv[] = { (char *)GO_CORE, (char *)"run", (char *)"-c",
                     (char *)GO_CORE_CONFIG, NULL };
    int old_flags = fcntl(backend->tun_fd, F_GETFD, 0);
    if (old_flags < 0 || fcntl(backend->tun_fd, F_SETFD, old_flags & ~FD_CLOEXEC) != 0)
        return -1;
    snprintf(fd_text, sizeof fd_text, "%d", backend->tun_fd);
    if (setenv("XRAY_TUN_FD", fd_text, 1) != 0) {
        (void)fcntl(backend->tun_fd, F_SETFD, old_flags);
        return -1;
    }
    int rc = posix_spawn(&backend->child, GO_CORE, NULL, NULL, argv, environ);
    unsetenv("XRAY_TUN_FD");
    /* only the core inherits the tunnel; pfctl, route and senko-kick spawn later */
    (void)fcntl(backend->tun_fd, F_SETFD, old_flags);
    return rc == 0 ? 0 : -1;
}

int go_backend_start(go_backend_t *backend, const vl_server_t *server,
                     const char *endpoint_ip, const ruleset_t *rules,
                     char *reason, size_t reason_cap) {
    char ifname[32];
    char gateway[64];
    char *config = NULL;
    awg_config_t route_config;
    if (!backend || !server || !endpoint_ip) return -1;
    go_backend_stop(backend);
    memset(backend, 0, sizeof *backend);
    backend->tun_fd = -1;

    if (!go_backend_supported()) {
        set_reason(reason, reason_cap, "the go backend core requires ios 12 or newer on arm64");
        return -1;
    }
    if (access(GO_CORE, X_OK) != 0) {
        set_reason(reason, reason_cap, "the bundled go core is missing or not executable");
        return -1;
    }
    if (awg_route_gateway_for_endpoint(endpoint_ip, gateway, sizeof gateway) != 0) {
        set_reason(reason, reason_cap, "physical network gateway could not be determined");
        return -1;
    }
    backend->tun_fd = awg_utun_open(ifname, sizeof ifname);
    if (backend->tun_fd < 0) {
        set_reason(reason, reason_cap, "utun interface could not be opened");
        return -1;
    }
    memset(&route_config, 0, sizeof route_config);
    snprintf(route_config.addresses[0], sizeof route_config.addresses[0], "198.18.0.1/32");
    snprintf(route_config.addresses[1], sizeof route_config.addresses[1], "fd00::1/128");
    route_config.address_count = 2;
    route_config.mtu = 1500;
    config = (char *)malloc(GO_CONFIG_MAX);
    if (!config) {
        set_reason(reason, reason_cap, "the routing configuration is too large for memory");
        goto fail;
    }
    if (awg_route_plan_build(&route_config, ifname, endpoint_ip, gateway,
                             &backend->route) != 0 ||
        go_config_render_rules(server, endpoint_ip, ifname, rules,
                               config, GO_CONFIG_MAX) != 0) {
        set_reason(reason, reason_cap, "selected profile could not be converted for the TUN core");
        goto fail;
    }
    if (write_config(config) != 0) {
        set_reason(reason, reason_cap, "secure runtime configuration could not be written");
        goto fail;
    }
    free(config);
    config = NULL;
    if (spawn_core(backend) != 0) {
        set_reason(reason, reason_cap, "the go core could not be started");
        goto fail;
    }
    if (awg_route_plan_up(&backend->route) != 0) {
        set_reason(reason, reason_cap, "utun routes could not be installed");
        goto fail;
    }
    backend->active = 1;
    fprintf(stderr, "senkod: go backend on %s, endpoint pinned to %s\n",
            backend->route.ifname, endpoint_ip);
    return 0;

fail:
    free(config);
    go_backend_stop(backend);
    return -1;
}

void go_backend_stop(go_backend_t *backend) {
    if (!backend) return;
    if (backend->active) awg_route_plan_down(&backend->route);
    backend->active = 0;
    memset(&backend->upload, 0, sizeof backend->upload);
    memset(&backend->download, 0, sizeof backend->download);
    if (backend->child > 0) {
        int status;
        (void)kill(backend->child, SIGTERM);
        for (int i = 0; i < 50; ++i) {
            pid_t waited = waitpid(backend->child, &status, WNOHANG);
            if (waited == backend->child || (waited < 0 && errno == ECHILD)) {
                backend->child = 0;
                break;
            }
            (void)poll(NULL, 0, 10);
        }
        if (backend->child > 0) {
            (void)kill(backend->child, SIGKILL);
            while (waitpid(backend->child, &status, 0) < 0 && errno == EINTR) {}
            backend->child = 0;
        }
    }
    if (backend->tun_fd >= 0) {
        close(backend->tun_fd);
        backend->tun_fd = -1;
    }
    unlink(GO_CORE_CONFIG);
}

int go_backend_bypass_add_ipv4(go_backend_t *backend, const char *ip) {
    char literal[INET_ADDRSTRLEN];
    if (!backend || !backend->active || !backend->route.gateway[0] ||
        !net_ipv4_literal(ip, literal, sizeof literal))
        return -1;
    /* the endpoint route is installed for the life of the tunnel, so a probe
       to that same address must not remove it on cleanup */
    if (strcmp(literal, backend->route.endpoint) == 0) return 0;
    return awg_pfroute_host4(1, literal, backend->route.gateway);
}

void go_backend_bypass_remove_ipv4(go_backend_t *backend, const char *ip) {
    char literal[INET_ADDRSTRLEN];
    if (!backend || !backend->route.gateway[0] ||
        !net_ipv4_literal(ip, literal, sizeof literal))
        return;
    if (strcmp(literal, backend->route.endpoint) == 0) return;
    (void)awg_pfroute_host4(0, literal, backend->route.gateway);
}

int go_backend_running(go_backend_t *backend) {
    int status;
    pid_t waited;
    if (!backend || !backend->active || backend->child <= 0) return 0;
    /* forgetting the pid on eintr would orphan a core that still holds the tunnel */
    while ((waited = waitpid(backend->child, &status, WNOHANG)) < 0 && errno == EINTR) {}
    if (waited == 0) return 1;
    backend->child = 0;
    return 0;
}

int go_backend_stats(go_backend_t *backend, uint64_t *up, uint64_t *down) {
    if (up) *up = 0;
    if (down) *down = 0;
    if (!backend || !up || !down) return -1;
    if (!backend->active) return 0;
#ifdef __APPLE__
    struct ifaddrs *addresses = NULL;
    int found = 0;
    if (getifaddrs(&addresses) != 0) return -1;
    for (struct ifaddrs *a = addresses; a; a = a->ifa_next) {
        if (!a->ifa_addr || !a->ifa_data || !a->ifa_name ||
            a->ifa_addr->sa_family != AF_LINK ||
            strcmp(a->ifa_name, backend->route.ifname) != 0) continue;
        const struct if_data *data = (const struct if_data *)a->ifa_data;
        /* utun output leaves the device stack; input returns from the core */
        traffic_counter_update(&backend->upload, data->ifi_obytes);
        traffic_counter_update(&backend->download, data->ifi_ibytes);
        found = 1;
        break;
    }
    freeifaddrs(addresses);
    if (!found) return -1;
    *up = backend->upload.bytes;
    *down = backend->download.bytes;
    return 0;
#else
    return -1;
#endif
}
