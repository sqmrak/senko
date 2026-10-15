#define _DEFAULT_SOURCE

#include "senko_core_backend.h"
#include "senko_core_config.h"
#include "awg_utun.h"
#include "awg_pfroute.h"
#include "vpn_conflict.h"
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
#include <dlfcn.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

extern char **environ;

#define SENKO_CORE_BIN SENKO_USR_LIB "/senko-core"
/* postinst puts a real copy here when rootful ios 12 kills the payload
   binary at exec with status 137 */
#define SENKO_CORE_ROOTFUL_COPY "/usr/lib/senko-core"
#define SENKO_CORE_CONFIG_PATH "/var/run/senko-core.json"
#define SENKO_CORE_CONFIG_MAX (2 * 1024 * 1024)

static void set_reason(char *reason, size_t cap, const char *value) {
    if (reason && cap) snprintf(reason, cap, "%s", value ? value : "senko-core failed");
}

int senko_core_backend_supported(void) {
#if defined(__LP64__)
    return senko_ios_major() >= 12;
#else
    return 0;
#endif
}

static const char *core_binary(void) {
    if (strcmp(SENKO_JBROOT, "") != 0 && access(SENKO_CORE_ROOTFUL_COPY, X_OK) == 0)
        return SENKO_CORE_ROOTFUL_COPY;
    return SENKO_CORE_BIN;
}

/* ios 12 to 14 lack SecTrustCopyCertificateChain, which go's system verifier
   binds lazily, so the first server certificate check kills senko-core with a
   dyld symbol error. those systems verify against the bundled roots instead */
static int core_roots_file(const char **file) {
    *file = NULL;
#ifdef __APPLE__
    static const char bundle[] = SENKO_USR_LIB "/senko/cacert.pem";
    static const char security_path[] = "/System/Library/Frameworks/Security.framework/Security";
    void *security = dlopen(security_path, RTLD_LAZY);
    if (security && dlsym(security, "SecTrustCopyCertificateChain")) return 0;
    if (access(bundle, R_OK) != 0) return -1;
    *file = bundle;
#endif
    return 0;
}

static int write_config(const char *contents) {
    char temporary[128];
    int n = snprintf(temporary, sizeof temporary, "%s.%ld", SENKO_CORE_CONFIG_PATH, (long)getpid());
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
    if (fsync(fd) != 0 || close(fd) != 0 || rename(temporary, SENKO_CORE_CONFIG_PATH) != 0) {
        unlink(temporary);
        return -1;
    }
    return 0;
}

static int spawn_core(senko_core_backend_t *backend, const char *geo_dir) {
    char fd_text[24];
    const char *binary = core_binary();
    char *argv[] = { (char *)binary, (char *)"run", (char *)"-c",
                     (char *)SENKO_CORE_CONFIG_PATH, NULL };
    int old_flags = fcntl(backend->tun_fd, F_GETFD, 0);
    if (old_flags < 0 || fcntl(backend->tun_fd, F_SETFD, old_flags & ~FD_CLOEXEC) != 0)
        return -1;
    snprintf(fd_text, sizeof fd_text, "%d", backend->tun_fd);
    if (setenv("XRAY_TUN_FD", fd_text, 1) != 0) {
        (void)fcntl(backend->tun_fd, F_SETFD, old_flags);
        return -1;
    }
    /* xray finds geosite.dat and geoip.dat in its asset directory */
    int geo = geo_dir && geo_dir[0] && setenv("XRAY_LOCATION_ASSET", geo_dir, 1) == 0;
    int rc = posix_spawn(&backend->child, binary, NULL, NULL, argv, environ);
    unsetenv("XRAY_TUN_FD");
    if (geo) unsetenv("XRAY_LOCATION_ASSET");
    /* only the core inherits the tunnel; later children must not */
    (void)fcntl(backend->tun_fd, F_SETFD, old_flags);
    return rc == 0 ? 0 : -1;
}

int senko_core_backend_start(senko_core_backend_t *backend, const vl_server_t *server,
                     const char *endpoint_ip, const char *dns_server,
                     const ruleset_t *rules, const char *geo_dir,
                     char *reason, size_t reason_cap) {
    char ifname[32];
    char gateway[64];
    char *config = NULL;
    const char *roots = NULL;
    awg_config_t route_config;
    if (!backend || !server || !endpoint_ip || !dns_server) return -1;
    senko_core_backend_stop(backend);
    memset(backend, 0, sizeof *backend);
    backend->tun_fd = -1;

    if (!senko_core_backend_supported()) {
        set_reason(reason, reason_cap, "senko-core requires ios 12 or newer on arm64");
        return -1;
    }
    if (access(core_binary(), X_OK) != 0) {
        set_reason(reason, reason_cap, "the bundled senko-core is missing or not executable");
        return -1;
    }
    if (core_roots_file(&roots) != 0) {
        set_reason(reason, reason_cap, "the bundled TLS roots are missing, senko-core cannot verify servers on this ios");
        return -1;
    }
    if (awg_route_gateway_for_endpoint(endpoint_ip, gateway, sizeof gateway) != 0) {
        set_reason(reason, reason_cap,
                   "no route to the server, check that wifi or cellular is connected");
        if (reason && reason_cap) vpn_conflict_explain(reason, reason_cap, NULL);
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
    config = (char *)malloc(SENKO_CORE_CONFIG_MAX);
    if (!config) {
        set_reason(reason, reason_cap, "the routing configuration is too large for memory");
        goto fail;
    }
    if (awg_route_plan_build(&route_config, ifname, endpoint_ip, gateway,
                             &backend->route) != 0 ||
        senko_core_config_render_rules(server, endpoint_ip, ifname, rules, geo_dir,
                               roots, config, SENKO_CORE_CONFIG_MAX) != 0) {
        set_reason(reason, reason_cap, "selected profile could not be converted for the TUN core");
        goto fail;
    }
    if (write_config(config) != 0) {
        set_reason(reason, reason_cap, "secure runtime configuration could not be written");
        goto fail;
    }
    free(config);
    config = NULL;
    if (spawn_core(backend, geo_dir) != 0) {
        set_reason(reason, reason_cap, "senko-core could not be started");
        goto fail;
    }
    if (awg_route_plan_up(&backend->route) != 0) {
        set_reason(reason, reason_cap, "utun routes could not be installed");
        if (reason && reason_cap) vpn_conflict_explain(reason, reason_cap, ifname);
        goto fail;
    }
    backend->active = 1;
    if (utun_dns_publish(&backend->dns, ifname, backend->route.ipv4,
                         backend->route.ipv4, dns_server,
                         reason, reason_cap) != 0)
        goto fail;
    fprintf(stderr, "senkod: senko-core on %s, endpoint pinned to %s\n",
            backend->route.ifname, endpoint_ip);
    return 0;

fail:
    free(config);
    senko_core_backend_stop(backend);
    return -1;
}

void senko_core_backend_stop(senko_core_backend_t *backend) {
    if (!backend) return;
    utun_dns_withdraw(&backend->dns);
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
    unlink(SENKO_CORE_CONFIG_PATH);
}

int senko_core_backend_bypass_add_ipv4(senko_core_backend_t *backend, const char *ip) {
    char literal[INET_ADDRSTRLEN];
    if (!backend || !backend->active || !backend->route.gateway[0] ||
        !net_ipv4_literal(ip, literal, sizeof literal))
        return -1;
    /* the endpoint route is installed for the life of the tunnel, so a probe
       to that same address must not remove it on cleanup */
    if (strcmp(literal, backend->route.endpoint) == 0) return 0;
    return awg_pfroute_host4(1, literal, backend->route.gateway);
}

void senko_core_backend_bypass_remove_ipv4(senko_core_backend_t *backend, const char *ip) {
    char literal[INET_ADDRSTRLEN];
    if (!backend || !backend->route.gateway[0] ||
        !net_ipv4_literal(ip, literal, sizeof literal))
        return;
    if (strcmp(literal, backend->route.endpoint) == 0) return;
    (void)awg_pfroute_host4(0, literal, backend->route.gateway);
}

int senko_core_backend_running(senko_core_backend_t *backend) {
    int status;
    pid_t waited;
    if (!backend || !backend->active || backend->child <= 0) return 0;
    /* forgetting the pid on eintr would orphan a core that still holds the tunnel */
    while ((waited = waitpid(backend->child, &status, WNOHANG)) < 0 && errno == EINTR) {}
    if (waited == 0) return 1;
    /* a binary the kernel kills at exec still spawns, so the exit status is
       the only trace of why the tunnel is gone */
    if (waited == backend->child) {
        if (WIFSIGNALED(status))
            fprintf(stderr, "senkod: senko-core killed by signal %d\n", WTERMSIG(status));
        else if (WIFEXITED(status))
            fprintf(stderr, "senkod: senko-core exited with status %d\n", WEXITSTATUS(status));
    }
    backend->child = 0;
    return 0;
}

int senko_core_backend_stats(senko_core_backend_t *backend, uint64_t *up, uint64_t *down) {
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
