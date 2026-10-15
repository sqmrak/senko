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
#include "../common/senko_paths.h"
#include "dialer.h"
#include "loop.h"
#include "proc_detach.h"
#include "senkod_helper.h"
#include "storefile.h"
#include "settings.h"
#include "status.h"

#include <openssl/crypto.h>

#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;
/* the managed loop sleeps in poll() with no timeout while idle, so a signal
   that lands between the g_stop check and poll() has to wake it through here */
static int g_signal_pipe[2] = { -1, -1 };

static void on_signal(int sig) {
    (void)sig;
    int saved = errno;
    g_stop = 1;
    if (g_signal_pipe[1] >= 0) {
        ssize_t wrote = write(g_signal_pipe[1], "s", 1);
        (void)wrote; /* a full pipe already holds the wakeup */
    }
    errno = saved;
}

static int open_signal_pipe(void) {
    if (pipe(g_signal_pipe) != 0) return -1;
    for (int i = 0; i < 2; ++i) {
        int fl = fcntl(g_signal_pipe[i], F_GETFL, 0);
        if (fl < 0 || fcntl(g_signal_pipe[i], F_SETFL, fl | O_NONBLOCK) != 0 ||
            fcntl(g_signal_pipe[i], F_SETFD, FD_CLOEXEC) != 0) {
            close(g_signal_pipe[0]);
            close(g_signal_pipe[1]);
            g_signal_pipe[0] = g_signal_pipe[1] = -1;
            return -1;
        }
    }
    return 0;
}

static void install_signals(void) {
    signal(SIGPIPE, SIG_IGN); /* ignore broken client sockets */
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
}

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
    if (cfg_proto_uses_uuid(srv.proto)) {
        if (vless_uuid_parse(srv.uuid, uuid) != VLESS_OK) {
            fprintf(stderr, "bad uuid in link\n");
            return 2;
        }
    }

    dialer_ctx_t dctx;
    dialer_set_target(&dctx, srv.host, srv.port);

    /* heap allocation avoids overflowing the small default stack on ios 5 */
    static loop_t lp;
    if (loop_init(&lp, (uint16_t)port, 0, vt, dialer_connect, &dctx,
                  srv.proto, uuid, srv.flow, srv.user, srv.pass) != LOOP_OK) {
        fprintf(stderr, "failed to bind socks listener on port %d\n", port);
        return 1;
    }
    loop_set_tls(&lp, srv.sni, srv.fp, srv.pbk, srv.sid, srv.path, srv.ws_host,
                 srv.mode, srv.host, srv.insecure);

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

static int run_managed(const char *ctl_path, const char *config_path,
                       daemon_settings_t *settings) {
    if (!settings) return 2;
    /* the tunnel has to outlive whatever started it */
    senko_proc_detach();
    int port = (int)settings->socks_port;
    int socks_public = settings->socks_public;
    /* stale socket files survive crashes, so a live connect decides ownership */
    int check_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (check_fd < 0)
        fprintf(stderr, "senkod: control socket check skipped: %s\n", strerror(errno));
    if (check_fd >= 0) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof addr);
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, ctl_path, sizeof addr.sun_path - 1);
        if (connect(check_fd, (struct sockaddr *)&addr, sizeof addr) == 0) {
            close(check_fd);
            fprintf(stderr, "senkod: already running\n");
            return 0;
        } else {
            int connect_errno = errno;
            close(check_fd);
            if (connect_errno == ENOENT || connect_errno == ECONNREFUSED) {
                unlink(ctl_path);
            } else {
/* launchd restarts a daemon that exits, so a silent exit here left an empty
   log and a restart every few seconds with nothing to say why */
                fprintf(stderr, "senkod: control socket %s is unusable: %s\n",
                        ctl_path, strerror(connect_errno));
                return 1;
            }
        }
    }

    uint8_t zero_uuid[VLESS_UUID_LEN];
    memset(zero_uuid, 0, sizeof zero_uuid);

    /* delaying activation prevents traffic from leaving through an unselected server */
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
    daemon_ctl_set_settings(&dc, settings);

    status_set(0);

    static ctl_server_t cs;
    if (ctl_server_init(&cs, ctl_path, daemon_ctl_apply, &dc) != CTLS_OK) {
        fprintf(stderr, "failed to bind control socket at %s\n", ctl_path);
        loop_close(&lp);
        return 1;
    }

    ctl_server_set_persist(&cs, daemon_ctl_persist);

    ctl_server_set_fetch(&cs, daemon_ctl_fetch);

    ctl_server_set_probe(&cs, daemon_ctl_probe);
    ctl_server_set_server_probe(&cs, daemon_ctl_probe_server);

    ctl_server_set_verify(&cs, daemon_ctl_verify_tunnel);

    ctl_server_set_tunnel_probe(&cs, daemon_ctl_ping_tunnel);

    ctl_server_set_backup(&cs, daemon_ctl_backup);
    ctl_server_set_check(&cs, daemon_ctl_check);
    ctl_server_set_reason(&cs, daemon_ctl_last_reason);
/* one live copy of the settings from here on: the daemon writes it, the control
   server reads it for the SETTINGS dump and for the schedules it runs */
    ctl_server_set_settings(&cs, &dc.settings);
    ctl_server_set_diag(&cs, daemon_ctl_diag);
    ctl_server_set_fwconf(&cs, daemon_ctl_fwconf);
    ctl_server_set_flush(&cs, daemon_ctl_flush);
    ctl_server_set_native_config(&cs, daemon_ctl_native_config);
    ctl_server_set_awg(&cs, daemon_ctl_awg, daemon_ctl_awg_busy);
    ctl_server_set_stats(&cs, daemon_ctl_stats);
    daemon_ctl_set_rules(&dc, &cs.engine.store.rules);
    daemon_ctl_set_store(&dc, &cs.engine.store, SENKO_GEO_DIR);
    ctl_server_set_geo(&cs, daemon_ctl_geo_fetch, SENKO_GEO_DIR);

    if (config_path && config_path[0]) {
/* the settings were merged from this file and the command line before the
   listener was bound, and reading them again here would put the file back on
   top of the arguments */
        if (storefile_load(&cs.engine.store, NULL, config_path) == STOREFILE_OK)
            fprintf(stderr, "senkod: loaded %zu server(s) from %s\n",
                    cs.engine.store.n, config_path);
    }

    install_signals();
    if (socks_public) {
        fprintf(stderr,
                "senkod: WARNING socks_public=1 binds SOCKS on 0.0.0.0 "
                "(LAN-reachable; disable unless intentional)\n");
    }
    fprintf(stderr, "senkod: managed mode. socks5 on %s:%u, control at %s\n",
            socks_public ? "0.0.0.0" : "127.0.0.1",
            loop_listen_port(&lp), ctl_path);

/* a daemon started by launchd after a reboot has no client to ask for the
   tunnel, so the stored selection is what brings routing back. an amneziawg
   profile the previous senkod left running (an update, a crash) is what the
   user chose last, so it wins */
    if (!daemon_ctl_awg_restore(&dc) && dc.settings.auto_connect) {
        if (ctl_server_restore_tunnel(&cs) == 0)
            fprintf(stderr, "senkod: auto-connected to server %d\n",
                    cs.engine.store.selected);
        else
            fprintf(stderr, "senkod: auto-connect found no server to start\n");
    }

    if (open_signal_pipe() != 0) {
        fprintf(stderr, "senkod: signal pipe failed (errno %d)\n", errno);
        daemon_ctl_shutdown(&dc);
        ctl_server_close(&cs);
        loop_close(&lp);
        return 1;
    }

/* one poll for every owner on this thread, waiting only as long as the
   nearest deadline any of them has: a fixed beat woke an idle ios device
   many times a second */
    while (!g_stop) {
        struct pollfd pfd[2 + LOOP_POLL_MAX + CTL_SERVER_POLL_MAX];
        int timeout_ms = daemon_ctl_wait_ms(&dc);
        size_t n = 0;
        pfd[n].fd = g_signal_pipe[0];
        pfd[n].events = POLLIN;
        pfd[n].revents = 0;
        n++;
        int ended_fd = daemon_ctl_wait_fd(&dc);
        if (ended_fd >= 0) {
            pfd[n].fd = ended_fd;
            pfd[n].events = POLLIN;
            pfd[n].revents = 0;
            n++;
        }
        size_t loop_at = n;
        size_t loop_n = loop_prepare(&lp, pfd + loop_at, LOOP_POLL_MAX, &timeout_ms);
        size_t ctl_at = loop_at + loop_n;
        size_t ctl_n = ctl_server_prepare(&cs, pfd + ctl_at, CTL_SERVER_POLL_MAX,
                                          &timeout_ms);
        if (loop_n == 0 || ctl_n == 0) break;

        int r = poll(pfd, (nfds_t)(ctl_at + ctl_n), timeout_ms);
        if (r < 0 && errno != EINTR) {
            fprintf(stderr, "senkod: poll failed (errno %d)\n", errno);
            break;
        }
        if (r > 0) loop_dispatch(&lp, pfd + loop_at, loop_n);
        /* helper jobs time out from dispatch, so it runs on a timeout too */
        if (r >= 0) ctl_server_dispatch(&cs, pfd + ctl_at, ctl_n);
/* the backend can die between two control commands, and the redial schedule
   lives with the store that knows which server to dial */
        if (daemon_ctl_maintain(&dc) != 0)
            ctl_server_tunnel_lost(&cs);
        ctl_server_tick(&cs);
    }

    fprintf(stderr, "senkod: shutting down\n");
    daemon_ctl_shutdown(&dc);
    if (config_path && config_path[0])
        storefile_save(&cs.engine.store, &dc.settings, config_path);
    ctl_server_close(&cs);
    loop_close(&lp);
    return 0;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage:\n"
        "  %s <vless://link> [socks_port]\n"
        "  %s --managed [--ctl <sockpath>] [--config <path>]\n"
        "       [--socks-port <n>] [--socks-public] [--dns-upstream <ip>]\n",
        argv0, argv0);
}

static void parse_managed_args(int argc, char **argv,
                               const char **ctl_path,
                               const char **config_path,
                               daemon_settings_t *settings) {
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
            /* the pf dns forwarder's port; skipped so its value is not read
               as a socks port by the fallback below */
            ++i;
        } else if (strcmp(argv[i], "--socks-public") == 0) {
            settings->socks_public = 1;
        } else {
            int p = atoi(argv[i]);
            if (p > 0 && p <= 65535) settings->socks_port = (uint16_t)p;
        }
    }
}

int main(int argc, char **argv) {
/* early initialization prevents ios 6 teardown from entering uninitialized cleanup */
    OPENSSL_init_crypto(OPENSSL_INIT_NO_ATEXIT, NULL);

    if (argc < 2) { usage(argv[0]); return 2; }

    int helper_rc = senkod_helper_main(argc, argv);
    if (helper_rc >= 0) return helper_rc;

    if (strcmp(argv[1], "--managed") == 0) {
/* the first line of every run, written before anything that can fail: an
   empty log then means launchd never started senkod, not that it died */
        fprintf(stderr, "senkod: starting, pid %d\n", (int)getpid());
        /* before the config and the loop tables are touched: on ios 16 the
           launchd limit is lower than the daemon's first allocations */
        senko_raise_memory_limit();
        daemon_settings_t settings;
        daemon_settings_defaults(&settings);
        const char *ctl_path = "/var/tmp/senkod.sock";
        const char *config_path = "";
        parse_managed_args(argc, argv, &ctl_path, &config_path, &settings);
        if (config_path[0]) {
            fprintf(stderr, "senkod: loading saved configuration\n");
            /* half a megabyte of servers does not fit the small default stack
               on ios 5, and the daemon reads the config once at startup */
            static store_t preload;
            store_init(&preload);
            storefile_status_t loaded = storefile_load(&preload, &settings, config_path);
            if (loaded != STOREFILE_OK)
                fprintf(stderr, "senkod: saved configuration could not be read (rc=%d)\n",
                        (int)loaded);
            else
                fprintf(stderr, "senkod: saved configuration read\n");
            parse_managed_args(argc, argv, &ctl_path, &config_path, &settings);
        }
        return run_managed(ctl_path, config_path, &settings);
    }

    int port = SENKO_DEFAULT_SOCKS_PORT;
    if (argc >= 3) {
        int p = atoi(argv[2]);
        if (p > 0 && p <= 65535) port = p;
    }
    return run_single(argv[1], port);
}
