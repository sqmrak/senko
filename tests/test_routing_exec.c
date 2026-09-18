#include "routing_exec.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int g_fail = 0;

static void ok(const char *what, int cond) {
    if (cond) return;
    g_fail++;
    fprintf(stderr, "FAIL %s\n", what);
}

static int bind_udp_port(int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr("127.0.0.1");
    sa.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int find_free_udp_range(int *start_out) {
    for (int p = 30000; p < 50000; ++p) {
        int a = bind_udp_port(p);
        int b = bind_udp_port(p + 1);
        int c = bind_udp_port(p + 2);
        if (a >= 0 && b >= 0 && c >= 0) {
            close(a);
            close(b);
            close(c);
            *start_out = p;
            return 0;
        }
        if (a >= 0) close(a);
        if (b >= 0) close(b);
        if (c >= 0) close(c);
    }
    return -1;
}

int main(void) {
    char pf[8192];
    size_t pf_len = 0;
    char ifnames[1][32] = {{ "en0" }};
    ruleset_t policy;
    ruleset_init(&policy);
    ok("add direct cidr",
       ruleset_add_text(&policy, "direct ip-cidr 198.51.100.0/24", 32, NULL) == RULES_OK);
    ok("add first block cidr",
       ruleset_add_text(&policy, "block ip-cidr 192.0.2.0/24", 28, NULL) == RULES_OK);
    ok("add second block cidr",
       ruleset_add_text(&policy, "block ip-cidr 203.0.113.128/25", 32, NULL) == RULES_OK);
    ok("add conflicting direct cidr",
       ruleset_add_text(&policy, "direct ip-cidr 192.0.2.0/25",
                        strlen("direct ip-cidr 192.0.2.0/25"), NULL) == RULES_OK);
    for (int mode = 0; mode < ROUTING_PF_MODE_COUNT; ++mode) {
        int rc = routing_pf_conf("203.0.113.10,203.0.113.11", ifnames, 1,
                                 41001, 41002, (routing_pf_mode_t)mode,
                                 pf, sizeof pf, &pf_len);
        ok("pf config generation", rc == ROUTING_OK && pf_len > 0 && pf[pf_len] == '\0');
        ok("pf bounds idle states", strstr(pf, "tcp.established 7200") != NULL);
        if (mode != ROUTING_PF_COMPAT_RDR) {
            ok("pf keeps local traffic in", strstr(pf, "pass in quick on en0 inet from <senko_bypass>") != NULL);
            ok("pf keeps local traffic out", strstr(pf, "pass out quick on en0 inet from any to <senko_bypass>") != NULL);
        }
    }

    ok("pf compiles cidr tables",
       routing_pf_conf_rules("203.0.113.10", &policy, ifnames, 1,
                             41001, 41002, ROUTING_PF_ROUTE_TO_LO0,
                             pf, sizeof pf, &pf_len) == ROUTING_OK &&
       strstr(pf, "198.51.100.0/24") != NULL &&
       strstr(pf, "192.0.2.0/25") == NULL &&
       strstr(pf, "table <senko_block> persist { 192.0.2.0/24, 203.0.113.128/25 }") != NULL);
    const char *block_rule = strstr(pf, "block return out quick on en0 inet from any to <senko_block>");
    const char *translation = strstr(pf, "nat on en0");
    ok("pf rejects blocked cidrs before redirect", block_rule && translation && block_rule < translation);

    ok("compat pf keeps cidr policy without tables",
       routing_pf_conf_rules("203.0.113.10", &policy, ifnames, 1,
                             41001, 41002, ROUTING_PF_COMPAT_RDR,
                             pf, sizeof pf, &pf_len) == ROUTING_OK &&
       strstr(pf, "<senko_block>") == NULL &&
       strstr(pf, "block return out quick on en0 inet from any to { 192.0.2.0/24, 203.0.113.128/25 }") != NULL &&
       strstr(pf, "198.51.100.0/24") != NULL);
    ok("compat pf avoids tables", routing_pf_conf("203.0.113.10", ifnames, 1,
                                                    41001, 41002,
                                                    ROUTING_PF_COMPAT_RDR,
                                                    pf, sizeof pf, &pf_len) == ROUTING_OK &&
       strstr(pf, "<senko_bypass>") == NULL &&
       strstr(pf, "inet6") == NULL &&
       strstr(pf, "route-to") == NULL &&
       strstr(pf, "203.0.113.10") != NULL);

    ok("anchor pf generation", routing_pf_anchor_conf("203.0.113.10", ifnames, 1,
                                                       41001, 41002,
                                                       ROUTING_PF_COMPAT_RDR,
                                                       pf, sizeof pf, &pf_len) == ROUTING_OK &&
       pf_len > 0 && strstr(pf, "set timeout") == NULL &&
       strstr(pf, "rdr pass on en0") != NULL);

    ok("anchor duplicates cidr block policy",
       routing_pf_anchor_conf_rules("203.0.113.10", &policy, ifnames, 1,
                                    41001, 41002, ROUTING_PF_LEGACY_RDR,
                                    pf, sizeof pf, &pf_len) == ROUTING_OK &&
       strstr(pf, "table <senko_block>") != NULL &&
       strstr(pf, "block return out quick on en0 inet from any to <senko_block>") != NULL);

    int dynamic_tcp = routing_pick_free_port(0, 0);
    ok("dynamic tcp port", dynamic_tcp > 0 && dynamic_tcp <= 65535);
    int dynamic_udp = routing_pick_free_udp_port(0, 0);
    ok("dynamic udp port", dynamic_udp > 0 && dynamic_udp <= 65535);

    int start = 0;
    ok("free udp range", find_free_udp_range(&start) == 0);

    int first = bind_udp_port(start);
    ok("occupy first udp", first >= 0);
    int picked = routing_pick_free_udp_port(start, start + 2);
    ok("skip occupied udp", picked == start + 1 || picked == start + 2);
    if (first >= 0) close(first);

    int fds[3];
    fds[0] = bind_udp_port(start);
    fds[1] = bind_udp_port(start + 1);
    fds[2] = bind_udp_port(start + 2);
    ok("occupy udp range", fds[0] >= 0 && fds[1] >= 0 && fds[2] >= 0);
    picked = routing_pick_free_udp_port(start, start + 2);
    ok("udp range exhausted", picked == -1);
    for (int i = 0; i < 3; ++i)
        if (fds[i] >= 0) close(fds[i]);

    /* the live bypass rule reuses ROUTING_IPFW_BASE + 1, so it only works while
       every generated bypass stays below the catch-all fwd rule and inside the
       range clear_ipfw sweeps on teardown */
    routing_ipfw_rule_t rules[ROUTING_MAX_RULES];
    size_t count = 0;
    ok("ipfw ruleset builds",
       routing_ipfw_rules("203.0.113.7", 40000, 1080, 15353,
                          rules, ROUTING_MAX_RULES, &count) == ROUTING_OK);
    int ascending = 1;
    for (size_t i = 0; i < count; ++i) {
        ok("rule number is in the swept range",
           rules[i].number >= ROUTING_IPFW_BASE && rules[i].number <= 12030);
        if (i && rules[i].number <= rules[i - 1].number) ascending = 0;
    }
    ok("rules are emitted in evaluation order", ascending);
    ok("catch-all fwd is the last rule",
       count > 0 && rules[count - 1].number == ROUTING_IPFW_FWD &&
       strcmp(rules[count - 1].rule_plain,
              "fwd 127.0.0.1,40000 tcp from any to any") == 0);
    ok("live bypass number stays below the fwd rule",
       ROUTING_IPFW_BASE + 1 < ROUTING_IPFW_FWD);

    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("all routing_exec checks passed\n");
    return 0;
}
