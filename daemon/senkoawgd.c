#define _DEFAULT_SOURCE

#include "core/awg_config.h"
#include "core/awg_handshake.h"
#include "core/awg_tunnel.h"
#include "awg_route.h"
#include "awg_pfroute.h"
#include "awg_utun.h"
#include "vpn_icon.h"

#include <openssl/crypto.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop;

#define AWG_REKEY_INTERVAL_MS 120000L
#define AWG_REKEY_RETRY_MS 5000L
#define AWG_STATUS_PATH "/var/run/senkoawgd.status"

static void on_signal(int signal_number) {
    (void)signal_number;
    g_stop = 1;
}

static void write_status(const char *text) {
    FILE *f = fopen(AWG_STATUS_PATH, "w");
    if (!f) return;
    fprintf(f, "%s\n", text);
    fclose(f);
}

static void usage(const char *argv0) {
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "  %s --handshake <amneziawg.conf> [timeout_ms]\n", argv0);
    fprintf(stderr, "  %s --validate <amneziawg.conf>\n", argv0);
    fprintf(stderr, "  %s --run <amneziawg.conf> [timeout_ms]\n", argv0);
    fprintf(stderr, "  %s --interface-probe <amneziawg.conf>\n", argv0);
    fprintf(stderr, "  %s --route-probe <ipv4>\n", argv0);
    fprintf(stderr, "  %s --route-mutation-probe <destination-ipv4> <gateway-ipv4>\n", argv0);
    fprintf(stderr, "  %s --net-route-probe <amneziawg.conf> <network-ipv4> <netmask-ipv4>\n", argv0);
    fprintf(stderr, "  %s --route-plan-probe <amneziawg.conf> <endpoint-ipv4> <gateway-ipv4>\n", argv0);
}

static int open_endpoint(const awg_config_t *cfg, char *endpoint, size_t endpoint_cap) {
    char port[8];
    snprintf(port, sizeof port, "%u", cfg->endpoint_port);
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = AF_INET;
    if (getaddrinfo(cfg->endpoint_host, port, &hints, &res) != 0 || !res) return -1;
    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            struct sockaddr_in *peer = (struct sockaddr_in *)ai->ai_addr;
            if (inet_ntop(AF_INET, &peer->sin_addr, endpoint, endpoint_cap)) {
                freeaddrinfo(res);
                return fd;
            }
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return -1;
}

static int write_all(int fd, const uint8_t *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static long monotonic_millis(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return 0;
    return (long)tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

static int run_tunnel(const awg_config_t *cfg, int timeout_ms) {
    write_status("connecting");
    char ifname[32];
    int tun_fd = awg_utun_open(ifname, sizeof ifname);
    if (tun_fd < 0) {
        write_status("error utun unavailable");
        fprintf(stderr, "senkoawgd: utun unavailable (%s)\n", strerror(errno));
        return 1;
    }
    fprintf(stderr, "senkoawgd: created %s\n", ifname);
    char endpoint[64], gateway[64];
    int udp_fd = open_endpoint(cfg, endpoint, sizeof endpoint);
    if (udp_fd < 0) {
        write_status("error endpoint udp connect failed");
        fprintf(stderr, "senkoawgd: endpoint udp connect failed\n");
        close(tun_fd);
        return 1;
    }

    awg_tunnel_t tunnel;
    awg_tunnel_init(&tunnel, cfg);
    char reason[128];
    awg_hs_status_t hs = awg_handshake_establish_fd(udp_fd, cfg, timeout_ms,
                                                     &tunnel.handshake,
                                                     reason, sizeof reason);
    if (hs != AWG_HS_OK) {
        char status[160];
        snprintf(status, sizeof status, "error %s", reason);
        write_status(status);
        fprintf(stderr, "senkoawgd: %s\n", reason);
        close(udp_fd);
        close(tun_fd);
        return 1;
    }
    if (awg_route_gateway_for_endpoint(endpoint, gateway, sizeof gateway) != 0) {
        write_status("error physical gateway lookup failed");
        fprintf(stderr, "senkoawgd: physical gateway lookup failed\n");
        OPENSSL_cleanse(&tunnel, sizeof tunnel);
        close(udp_fd); close(tun_fd);
        return 1;
    }
    awg_route_plan_t route_plan;
    if (awg_route_plan_build(cfg, ifname, endpoint, gateway, &route_plan) != 0 ||
        awg_route_plan_up(&route_plan) != 0) {
        write_status("error route setup failed");
        fprintf(stderr, "senkoawgd: route setup failed\n");
        OPENSSL_cleanse(&tunnel, sizeof tunnel);
        close(udp_fd); close(tun_fd);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    static uint8_t framed[AWG_DATAGRAM_MAX + 4];
    static uint8_t wire[AWG_DATAGRAM_MAX];
    static uint8_t inner[AWG_DATAGRAM_MAX];
    fprintf(stderr, "senkoawgd: linked %s to %s:%u\n",
            ifname, cfg->endpoint_host, cfg->endpoint_port);
    write_status("connected");
    vpn_icon_set(1);
    long last_tx_ms = monotonic_millis();
    long last_handshake_ms = last_tx_ms;
    long last_rekey_attempt_ms = 0;
    while (!g_stop) {
        struct pollfd pfd[2];
        pfd[0].fd = tun_fd; pfd[0].events = POLLIN; pfd[0].revents = 0;
        pfd[1].fd = udp_fd; pfd[1].events = POLLIN; pfd[1].revents = 0;
        int pr = poll(pfd, 2, 1000);
        if (pr < 0 && errno == EINTR) continue;
        if (pr < 0) break;
        if (pfd[0].revents & POLLIN) {
            ssize_t got = read(tun_fd, framed, sizeof framed);
            if (got > 4) {
                size_t wire_len = 0;
                awg_tun_status_t tr = awg_tunnel_seal(&tunnel, framed + 4,
                                                       (size_t)got - 4,
                                                       wire, sizeof wire, &wire_len);
                if (tr == AWG_TUN_OK) {
                    if (send(udp_fd, wire, wire_len, 0) != (ssize_t)wire_len) break;
                    last_tx_ms = monotonic_millis();
                }
            }
        }
        if (pfd[1].revents & POLLIN) {
            ssize_t got = recv(udp_fd, wire, sizeof wire, 0);
            if (got > 0) {
                size_t inner_len = 0;
                awg_tun_status_t tr = awg_tunnel_open(&tunnel, wire, (size_t)got,
                                                       inner, sizeof inner, &inner_len);
                if (tr == AWG_TUN_OK && inner_len > 0) {
                    uint8_t version = inner[0] >> 4;
                    if (version == 4 || version == 6) {
                        uint32_t family = htonl(version == 4 ? AF_INET : AF_INET6);
                        memcpy(framed, &family, sizeof family);
                        memcpy(framed + 4, inner, inner_len);
                        if (write_all(tun_fd, framed, inner_len + 4) != 0) break;
                    }
                }
            }
        }
        if (cfg->persistent_keepalive &&
            monotonic_millis() - last_tx_ms >= (long)cfg->persistent_keepalive * 1000L) {
            size_t wire_len = 0;
            if (awg_tunnel_seal(&tunnel, NULL, 0, wire, sizeof wire, &wire_len) != AWG_TUN_OK ||
                send(udp_fd, wire, wire_len, 0) != (ssize_t)wire_len)
                break;
            last_tx_ms = monotonic_millis();
        }
        long now_ms = monotonic_millis();
        if (now_ms - last_handshake_ms >= AWG_REKEY_INTERVAL_MS &&
            now_ms - last_rekey_attempt_ms >= AWG_REKEY_RETRY_MS) {
            last_rekey_attempt_ms = now_ms;
            awg_handshake_t refreshed;
            if (awg_handshake_establish_fd(udp_fd, cfg, timeout_ms, &refreshed,
                                           reason, sizeof reason) == AWG_HS_OK) {
                OPENSSL_cleanse(&tunnel.handshake, sizeof tunnel.handshake);
                tunnel.handshake = refreshed;
                tunnel.send_counter = 0;
                tunnel.recv_counter = 0;
                tunnel.recv_window = 0;
                tunnel.have_recv_counter = 0;
                last_handshake_ms = now_ms;
                last_tx_ms = now_ms;
            } else {
                OPENSSL_cleanse(&refreshed, sizeof refreshed);
                fprintf(stderr, "senkoawgd: rekey deferred: %s\n", reason);
            }
        }
    }
    awg_route_plan_down(&route_plan);
    vpn_icon_set(0);
    write_status(g_stop ? "idle" : "error tunnel stopped");
    OPENSSL_cleanse(&tunnel, sizeof tunnel);
    close(udp_fd);
    close(tun_fd);
    return 0;
}

int main(int argc, char **argv) {
    OPENSSL_init_crypto(OPENSSL_INIT_NO_ATEXIT, NULL);
    if (argc < 2 || (strcmp(argv[1], "--handshake") != 0 &&
                     strcmp(argv[1], "--validate") != 0 &&
                     strcmp(argv[1], "--run") != 0 &&
                     strcmp(argv[1], "--interface-probe") != 0 &&
                     strcmp(argv[1], "--route-probe") != 0 &&
                     strcmp(argv[1], "--route-mutation-probe") != 0 &&
                     strcmp(argv[1], "--net-route-probe") != 0 &&
                     strcmp(argv[1], "--route-plan-probe") != 0)) {
        usage(argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "--route-probe") == 0) {
        char detail[160];
        int rc = awg_pfroute_probe_get4(argv[2], detail, sizeof detail);
        fprintf(stderr, "senkoawgd: route probe %s\n", detail);
        return rc == 0 ? 0 : 1;
    }
    if (argc < 3) { usage(argv[0]); return 2; }
    if (strcmp(argv[1], "--route-mutation-probe") == 0) {
        if (argc < 4) { usage(argv[0]); return 2; }
        char detail[160]; int rc = awg_pfroute_probe_host4(argv[2], argv[3], detail, sizeof detail);
        fprintf(stderr, "senkoawgd: route mutation %s\n", detail);
        return rc == 0 ? 0 : 1;
    }
    int timeout_ms = argc > 3 ? atoi(argv[3]) : 5000;
    awg_config_t cfg;
    char reason[128];
    awg_cfg_status_t cr = awg_config_load_file(argv[2], &cfg, reason, sizeof reason);
    if (cr != AWG_CFG_OK) {
        if (strcmp(argv[1], "--validate") == 0)
            printf("ERR config rejected: %s\n", reason);
        else
            fprintf(stderr, "senkoawgd: config rejected: %s\n", reason);
        return 2;
    }
    if (strcmp(argv[1], "--validate") == 0) {
        printf("VALID AmneziaWG native config\n");
        return 0;
    }
    if (strcmp(argv[1], "--interface-probe") == 0) {
        char ifname[32];
        int fd = awg_utun_open(ifname, sizeof ifname);
        awg_route_plan_t plan;
        int ok = fd >= 0 && awg_route_plan_for_interface(&cfg, ifname, &plan) == 0 &&
                 awg_route_interface_up(&plan) == 0;
        if (fd >= 0) close(fd);
        fprintf(stderr, "senkoawgd: interface probe %s\n", ok ? "ok" : "failed");
        return ok ? 0 : 1;
    }
    if (strcmp(argv[1], "--net-route-probe") == 0) {
        if (argc < 5) { usage(argv[0]); return 2; }
        char ifname[32];
        int fd = awg_utun_open(ifname, sizeof ifname);
        awg_route_plan_t plan;
        int ok = fd >= 0 && awg_route_plan_for_interface(&cfg, ifname, &plan) == 0 &&
                 awg_route_interface_up(&plan) == 0;
        if (ok) ok = awg_pfroute_net4_if(1, argv[3], argv[4], ifname) == 0;
        if (ok) ok = awg_pfroute_net4_if(0, argv[3], argv[4], ifname) == 0;
        if (fd >= 0) close(fd);
        fprintf(stderr, "senkoawgd: network route probe %s\n", ok ? "ok" : "failed");
        return ok ? 0 : 1;
    }
    if (strcmp(argv[1], "--route-plan-probe") == 0) {
        if (argc < 5) { usage(argv[0]); return 2; }
        char ifname[32];
        int fd = awg_utun_open(ifname, sizeof ifname);
        awg_route_plan_t plan;
        int ok = fd >= 0 && awg_route_plan_build(&cfg, ifname, argv[3], argv[4], &plan) == 0 &&
                 awg_route_plan_up(&plan) == 0;
        if (ok) awg_route_plan_down(&plan);
        if (fd >= 0) close(fd);
        fprintf(stderr, "senkoawgd: route plan probe %s\n", ok ? "ok" : "failed");
        return ok ? 0 : 1;
    }
    if (strcmp(argv[1], "--run") == 0)
        return run_tunnel(&cfg, timeout_ms);
    awg_hs_status_t hr = awg_handshake_probe(&cfg, timeout_ms, reason, sizeof reason);
    if (hr != AWG_HS_OK) {
        fprintf(stderr, "senkoawgd: %s\n", reason);
        return 1;
    }
    printf("senkoawgd: handshake accepted by %s:%u\n", cfg.endpoint_host, cfg.endpoint_port);
    return 0;
}
