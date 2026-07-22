#define _DEFAULT_SOURCE

#include "core/config.h"
#include "core/transport.h"
#include "core/transport_pick.h"
#include "core/vless.h"
#include <sys/socket.h>
#include <sys/un.h>
#include "core/store.h"
#include "ctl_server.h"
#include "daemon_ctl.h"
#include "dialer.h"
#include "loop.h"
#include "storefile.h"
#include "settings.h"
#include "vpn_icon.h"

#include <openssl/crypto.h>

#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

static void install_signals(void) {
    signal(SIGPIPE, SIG_IGN); /* ignore broken client sockets */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
}

/* run one server without the control daemon */
static int run_single(const char *link, int port) {
    vl_server_t srv;
    if (cfg_parse_link(link, &srv) != CFG_OK) {
        fprintf(stderr, "bad server link\n");
        return 2;
    }
    char reason[128];
    if (!cfg_validate_server(&srv, reason, sizeof reason)) {
        fprintf(stderr, "unsupported server: %s\n",
                reason[0] ? reason : "unknown");
        return 2;
    }
    const transport_vt_t *vt = transport_for_server(&srv);
    if (!vt) { fprintf(stderr, "unknown security mode\n"); return 2; }

    uint8_t uuid[VLESS_UUID_LEN];
    memset(uuid, 0, sizeof uuid);
    if (srv.proto == VL_PROTO_VLESS) {
        if (vless_uuid_parse(srv.uuid, uuid) != VLESS_OK) {
            fprintf(stderr, "bad uuid in link\n");
            return 2;
        }
    }

    dialer_ctx_t dctx;
    dialer_set_target(&dctx, srv.host, srv.port);

    /* keep the large loop state off the stack */
    static loop_t lp;
    if (loop_init(&lp, (uint16_t)port, 0, vt, dialer_connect, &dctx,
                  srv.proto, uuid, srv.flow, srv.user, srv.pass) != LOOP_OK) {
        fprintf(stderr, "failed to bind socks listener on port %d\n", port);
        return 1;
    }
    loop_set_tls(&lp, srv.sni, srv.fp, srv.pbk, srv.sid, srv.path, srv.ws_host,
                 srv.mode);

    install_signals();
    fprintf(stderr, "senkod: socks5 on 127.0.0.1:%u -> %s:%u (%s)\n",
            loop_listen_port(&lp), srv.host, srv.port,
            srv.remark[0] ? srv.remark : "server");

    while (!g_stop) {
        if (loop_step(&lp, 1000) != LOOP_OK) break;
    }
    fprintf(stderr, "senkod: shutting down\n");
    loop_close(&lp);
    return 0;
}

/* run the root daemon with a control socket */
static int run_managed(const char *ctl_path, const char *config_path,
                       int full_device, daemon_settings_t *settings) {
    if (!settings) return 2;
    int port = (int)settings->socks_port;
    int socks_public = settings->socks_public;
    /* a socket file alone does not prove that the daemon is alive */
    int check_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (check_fd >= 0) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof addr);
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, ctl_path, sizeof addr.sun_path - 1);
        if (connect(check_fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            setsockopt(check_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            const char *ping = "STATUS\n";
            (void)write(check_fd, ping, 7);
            char rbuf[64];
            ssize_t nr = read(check_fd, rbuf, sizeof rbuf - 1);
            close(check_fd);
            if (nr >= 6 && memcmp(rbuf, "STATE ", 6) == 0) {
                fprintf(stderr, "senkod: already running on %s\n", ctl_path);
                return 0;
            }
            if (nr == 0 || (nr < 0 && (errno == ECONNRESET || errno == EPIPE))) {
                fprintf(stderr, "senkod: stale ctl sock, taking over\n");
                unlink(ctl_path);
            } else {
                fprintf(stderr, "senkod: control peer did not answer safely\n");
                return 1;
            }
        } else {
            int connect_errno = errno;
            close(check_fd);
            if (connect_errno == ENOENT || connect_errno == ECONNREFUSED)
                unlink(ctl_path);
            else
                return 1;
        }
    }

    uint8_t zero_uuid[VLESS_UUID_LEN];
    memset(zero_uuid, 0, sizeof zero_uuid);

    /* bind SOCKS first, but keep it inactive until a server is selected */
    static loop_t lp;
    if (loop_init(&lp, (uint16_t)port, socks_public, &transport_tcp, dialer_connect, NULL,
                  VL_PROTO_VLESS, zero_uuid, NULL, NULL, NULL) != LOOP_OK) {
        fprintf(stderr, "failed to bind socks listener on port %d\n", port);
        return 1;
    }
    uint16_t actual_port = loop_listen_port(&lp);
    if (actual_port != (uint16_t)port) {
        fprintf(stderr, "senkod: socks port %d is busy; refusing duplicate daemon\n", port);
        loop_close(&lp);
        return 1;
    }
    loop_stop(&lp); /* inactive until a server is selected */

    daemon_ctl_t dc;
    daemon_ctl_init(&dc, &lp, config_path);
    daemon_ctl_set_full_device(&dc, full_device);
    daemon_ctl_set_settings(&dc, settings);

    vpn_icon_set(0);

    /* remove routing left behind by a crashed daemon */
    routing_exec_clear_stale();

    static ctl_server_t cs;
    if (ctl_server_init(&cs, ctl_path, daemon_ctl_apply, &dc) != CTLS_OK) {
        fprintf(stderr, "failed to bind control socket at %s\n", ctl_path);
        loop_close(&lp);
        return 1;
    }

    ctl_server_set_persist(&cs, daemon_ctl_persist);

    ctl_server_set_fetch(&cs, daemon_ctl_fetch);

    ctl_server_set_probe(&cs, daemon_ctl_probe);

    ctl_server_set_verify(&cs, daemon_ctl_verify_tunnel);

    ctl_server_set_tunnel_probe(&cs, daemon_ctl_ping_tunnel);

    if (config_path && config_path[0]) {
        if (storefile_load(&cs.engine.store, settings, config_path) == STOREFILE_OK)
            fprintf(stderr, "senkod: loaded %zu server(s) from %s\n",
                    cs.engine.store.n, config_path);
    }

    install_signals();
    if (socks_public) {
        fprintf(stderr,
                "senkod: WARNING socks_public=1 binds SOCKS on 0.0.0.0 "
                "(LAN-reachable; disable unless intentional)\n");
    }
    fprintf(stderr, "senkod: managed mode%s. socks5 on %s:%u, control at %s\n",
            full_device ? " (full-device routing)" : "",
            socks_public ? "0.0.0.0" : "127.0.0.1",
            loop_listen_port(&lp), ctl_path);

    /* selected server is restored by the ui when the user connects */
    while (!g_stop) {
        if (loop_step(&lp, 50) != LOOP_OK) break;
        ctl_server_step(&cs, 50);
    }

    fprintf(stderr, "senkod: shutting down\n");
    /* remove routing before closing the data loop */
    daemon_ctl_shutdown(&dc);
    if (config_path && config_path[0])
        storefile_save(&cs.engine.store, settings, config_path);
    ctl_server_close(&cs);
    loop_close(&lp);
    return 0;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage:\n"
        "  %s <vless://link> [socks_port]\n"
        "  %s --managed [--ctl <sockpath>] [--config <path>]\n"
        "       [--socks-port <n>] [--socks-public] [--dns-upstream <ip>]\n"
        "       [--dns-local-port <n>] [--full-device]\n",
        argv0, argv0);
}

static void parse_managed_args(int argc, char **argv,
                               const char **ctl_path,
                               const char **config_path,
                               daemon_settings_t *settings,
                               int *full_device) {
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--ctl") == 0 && i + 1 < argc) {
            *ctl_path = argv[++i];
        } else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            *config_path = argv[++i];
        } else if (strcmp(argv[i], "--socks-port") == 0 && i + 1 < argc) {
            int p = atoi(argv[++i]);
            if (p > 0 && p <= 65535) settings->socks_port = (uint16_t)p;
        } else if (strcmp(argv[i], "--dns-upstream") == 0 && i + 1 < argc) {
            const char *ip = argv[++i];
            struct in_addr a;
            if (inet_pton(AF_INET, ip, &a) == 1)
                snprintf(settings->dns_upstream, sizeof settings->dns_upstream,
                         "%s", ip);
        } else if (strcmp(argv[i], "--dns-local-port") == 0 && i + 1 < argc) {
            int p = atoi(argv[++i]);
            if (p > 0 && p <= 65535) settings->dns_local_port = (uint16_t)p;
        } else if (strcmp(argv[i], "--full-device") == 0) {
            *full_device = 1;
        } else if (strcmp(argv[i], "--socks-public") == 0) {
            settings->socks_public = 1;
        } else {
            int p = atoi(argv[i]);
            if (p > 0 && p <= 65535) settings->socks_port = (uint16_t)p;
        }
    }
}

int main(int argc, char **argv) {
/* initialize openssl early so ios6 teardown does not trip cleanup code */
    OPENSSL_init_crypto(OPENSSL_INIT_NO_ATEXIT, NULL);

    if (argc < 2) { usage(argv[0]); return 2; }

    if (strcmp(argv[1], "--managed") == 0) {
        daemon_settings_t settings;
        daemon_settings_defaults(&settings);
        const char *ctl_path = "/var/tmp/senkod.sock";
        const char *config_path = "";
        int full_device = 0;
        parse_managed_args(argc, argv, &ctl_path, &config_path, &settings, &full_device);
        if (config_path[0]) {
            store_t preload;
            store_init(&preload);
            storefile_load(&preload, &settings, config_path);
            parse_managed_args(argc, argv, &ctl_path, &config_path, &settings, &full_device);
        }
        return run_managed(ctl_path, config_path, full_device, &settings);
    }

    int port = SENKO_DEFAULT_SOCKS_PORT;
    if (argc >= 3) {
        int p = atoi(argv[2]);
        if (p > 0 && p <= 65535) port = p;
    }
    return run_single(argv[1], port);
}
