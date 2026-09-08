#define _DEFAULT_SOURCE

#include "routing_fwd.h"
#include "routing_exec.h"
#include "core/net_safe.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include "../common/senko_paths.h"

#define FWD_RULE_MIN 12000
#define FWD_RULE_MAX 12098
#define C_PROXY_STATE "/var/run/senko-c-proxy"

static int ipfw_del(int number) {
    const char *ipfw = routing_find_ipfw();
    if (!ipfw) return -1;
    char num[16];
    snprintf(num, sizeof num, "%d", number);
    char *argv[] = { (char *)ipfw, (char *)"-q", (char *)"delete", num, NULL };
    return routing_spawn(ipfw, argv);
}

static int ipfw_add(const char *body) {
    const char *ipfw = routing_find_ipfw();
    if (!ipfw) return -1;
    char rule[384];
    int n = snprintf(rule, sizeof rule, "%s", body);
    if (n < 0 || (size_t)n >= sizeof rule) return -1;
    char *save = NULL;
    char *words[40];
    int nw = 0;
    for (char *tok = strtok_r(rule, " ", &save); tok && nw < 39;
         tok = strtok_r(NULL, " ", &save))
        words[nw++] = tok;
    if (nw < 2) return -1;
    char *argv[44];
    int an = 0;
    argv[an++] = (char *)ipfw;
    argv[an++] = (char *)"-q";
    argv[an++] = (char *)"add";
    for (int i = 0; i < nw && an < 42; ++i) argv[an++] = words[i];
    argv[an] = NULL;
    return routing_spawn(ipfw, argv);
}

void routing_fwd_clear_rules(void) {
    (void)unlink(C_PROXY_STATE);
    for (int n = FWD_RULE_MAX; n >= FWD_RULE_MIN; --n)
        (void)ipfw_del(n);
}

static int application_proxy_available(void) {
    return access(SENKO_SUBSTRATE_DIR "/senkotlsfix.dylib", R_OK) == 0;
}

int routing_fwd_app_proxy_up(routing_fwd_t *st, int socks_port) {
    if (!st || socks_port <= 0 || socks_port > 65535) return -1;
    if (!application_proxy_available()) {
        fprintf(stderr, "senkod: c backend: application proxy hook is not installed\n");
        return -1;
    }
    char tmp[128];
    int n = snprintf(tmp, sizeof tmp, "%s.%ld", C_PROXY_STATE, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof tmp) return -1;
    (void)unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return -1;
    (void)fchmod(fd, 0644);
    char value[64];
    n = snprintf(value, sizeof value, "SENKO-C-PROXY-V1 %d\n", socks_port);
    int ok = n > 0 && (size_t)n < sizeof value && write(fd, value, (size_t)n) == n;
    if (ok) ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = 0;
    if (ok) ok = rename(tmp, C_PROXY_STATE) == 0;
    if (!ok) {
        (void)unlink(tmp);
        fprintf(stderr, "senkod: c backend: application proxy state failed: %s\n",
                strerror(errno));
        return -1;
    }
    memset(st, 0, sizeof *st);
    st->socks_port = socks_port;
    st->redir_port = socks_port;
    st->app_proxy = 1;
    st->active = 1;
    fprintf(stderr, "senkod: c backend: application proxy on 127.0.0.1:%d\n",
            socks_port);
    return 0;
}

static int add_bypasses(routing_fwd_t *st) {
    static const char *const nets[] = {
        "127.0.0.1", "10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"
    };
    char rule[256];
    for (size_t i = 0; i < sizeof nets / sizeof nets[0]; ++i) {
        if (snprintf(rule, sizeof rule, "%d allow tcp from any to %s",
                     FWD_RULE_MIN + (int)i, nets[i]) >= (int)sizeof rule)
            return -1;
        if (ipfw_add(rule) != 0) return -1;
    }

    char ips[4096];
    snprintf(ips, sizeof ips, "%s", st->server_ips);
    int slot = FWD_RULE_MIN + (int)(sizeof nets / sizeof nets[0]);
    char *save = NULL;
    for (char *tok = strtok_r(ips, ", ", &save); tok && slot <= FWD_RULE_MAX - 2;
         tok = strtok_r(NULL, ", ", &save)) {
        char ip[64];
        if (!net_ipv4_literal(tok, ip, sizeof ip)) continue;
        if (snprintf(rule, sizeof rule,
                     "%d allow tcp from any to %s", slot, ip) >= (int)sizeof rule)
            return -1;
        if (ipfw_add(rule) != 0) return -1;
        slot++;
    }
    st->next_bypass_slot = slot;
    return 0;
}

/* keep the tunnel's own destinations out of the catch-all fwd rule */
static void note_server_ip(routing_fwd_t *st, const char *ip) {
    if (!ip || !ip[0] || net_ip_list_contains(st->server_ips, ip)) return;
    size_t len = strlen(st->server_ips);
    size_t iplen = strlen(ip);
    if (len + iplen + 2 > sizeof st->server_ips) return;
    if (len) st->server_ips[len++] = ',';
    memcpy(st->server_ips + len, ip, iplen + 1);
}

int routing_fwd_up(routing_fwd_t *st, int socks_port,
                   const char *server_ip, const char *server_ips) {
    if (!st || !server_ip || !server_ips) return -1;
    if (!routing_find_ipfw()) return -1;

    int redir = routing_pick_free_port(0, 0);
    if (redir <= 0) return -1;

    memset(st, 0, sizeof *st);
    st->socks_port = socks_port;
    snprintf(st->server_ips, sizeof st->server_ips, "%s", server_ips);
    note_server_ip(st, server_ip);

    routing_fwd_clear_rules();

    if (add_bypasses(st) != 0) {
        fprintf(stderr, "senkod: c backend: ipfw bypass rules rejected\n");
        routing_fwd_clear_rules();
        return -1;
    }

    char rule[256];
    if (snprintf(rule, sizeof rule,
                 "%d check-state", FWD_RULE_MAX - 1) >= (int)sizeof rule ||
        ipfw_add(rule) != 0) {
        fprintf(stderr, "senkod: c backend: ipfw check-state rejected\n");
        routing_fwd_clear_rules();
        return -1;
    }

    /* dynamic states retain fwd action for replies without re-forwarding them */
    if (snprintf(rule, sizeof rule,
                 "%d fwd 127.0.0.1,%d tcp from any to any out setup keep-state",
                 FWD_RULE_MAX, redir) >= (int)sizeof rule ||
        ipfw_add(rule) != 0) {
        fprintf(stderr, "senkod: c backend: ipfw fwd rule rejected\n");
        routing_fwd_clear_rules();
        return -1;
    }

    if (routing_scopedroute_disable(st->scoped_route_prev,
                                    sizeof st->scoped_route_prev) != 0)
        fprintf(stderr, "senkod: c backend: scopedroute tweak failed, "
                        "ipfw fwd may not reach the listener\n");

    st->redir_port = redir;
    st->active = 1;
    fprintf(stderr, "senkod: c backend: ipfw fwd to 0.0.0.0:%d\n", redir);
    return 0;
}

void routing_fwd_down(routing_fwd_t *st) {
    if (!st || !st->active) return;
    (void)unlink(C_PROXY_STATE);
    if (!st->app_proxy) {
        routing_fwd_clear_rules();
        routing_scopedroute_restore(st->scoped_route_prev);
    }
    memset(st, 0, sizeof *st);
}

void routing_fwd_bypass_add_ipv4(routing_fwd_t *st, const char *ip) {
    if (!st || !st->active || st->app_proxy || !ip || !ip[0]) return;
    if (net_ip_list_contains(st->server_ips, ip)) return;
    if (st->next_bypass_slot > FWD_RULE_MAX - 2) return;
    char rule[256];
    if (snprintf(rule, sizeof rule, "%d allow tcp from any to %s",
                 st->next_bypass_slot, ip) >= (int)sizeof rule)
        return;
    if (ipfw_add(rule) != 0) return;
    st->next_bypass_slot++;
    note_server_ip(st, ip);
}
