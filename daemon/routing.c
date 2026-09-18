#include "routing.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* append both rule spellings while rejecting capacity or truncation */
static int add_rule(routing_ipfw_rule_t *rules, size_t cap, size_t *count,
                    int number, const char *body_fmt, ...) {
    if (*count >= cap) return -1;
    routing_ipfw_rule_t *r = &rules[*count];
    r->number = number;

    va_list ap;
    va_start(ap, body_fmt);
    int n = vsnprintf(r->rule_plain, sizeof r->rule_plain, body_fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof r->rule_plain) return -1;

/* retain a second spelling because ipfw keyword placement varies */
    n = snprintf(r->rule_out, sizeof r->rule_out, "%s out", r->rule_plain);
    if (n < 0 || (size_t)n >= sizeof r->rule_out) return -1;

    (*count)++;
    return 0;
}

routing_status_t routing_ipfw_rules(const char *server_ip,
                                    int redir_port, int socks_port,
                                    int dns_local_port,
                                    routing_ipfw_rule_t *rules, size_t cap,
                                    size_t *out_count) {
    if (!server_ip || !rules || !out_count || dns_local_port <= 0)
        return ROUTING_ERR_ARG;
    *out_count = 0;
    size_t c = 0;

/* keep bypasses first because lower ipfw numbers evaluate first */

    if (add_rule(rules, cap, &c, 12000, "allow tcp from any to 127.0.0.1") != 0)
        return ROUTING_ERR_SPACE;

/* bypass the vless server so tunnel packets cannot re-enter tproxy */
    if (add_rule(rules, cap, &c, 12001, "allow tcp from any to %s", server_ip) != 0)
        return ROUTING_ERR_SPACE;

    if (add_rule(rules, cap, &c, 12002, "allow tcp from any to 10.0.0.0/8") != 0)
        return ROUTING_ERR_SPACE;
    if (add_rule(rules, cap, &c, 12003, "allow tcp from any to 172.16.0.0/12") != 0)
        return ROUTING_ERR_SPACE;
    if (add_rule(rules, cap, &c, 12004, "allow tcp from any to 192.168.0.0/16") != 0)
        return ROUTING_ERR_SPACE;

    if (add_rule(rules, cap, &c, 12005, "allow tcp from any to 127.0.0.1 %d", socks_port) != 0)
        return ROUTING_ERR_SPACE;
    if (add_rule(rules, cap, &c, 12006, "allow tcp from any to 127.0.0.1 %d", redir_port) != 0)
        return ROUTING_ERR_SPACE;

    if (add_rule(rules, cap, &c, 12010,
                 "fwd 127.0.0.1,%d udp from any to any 53", dns_local_port) != 0)
        return ROUTING_ERR_SPACE;

    if (add_rule(rules, cap, &c, 12020, "fwd 127.0.0.1,%d tcp from any to any", redir_port) != 0)
        return ROUTING_ERR_SPACE;

    *out_count = c;
    return ROUTING_OK;
}

/* pf output is built in one buffer so partial rules never apply */

/* bounded appender into a string buffer. sets *err on overflow */
typedef struct { char *buf; size_t cap; size_t pos; int err; } pf_w_t;

static void pf_ap(pf_w_t *w, const char *fmt, ...) {
    if (w->err) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(w->buf + w->pos, w->cap - w->pos, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= w->cap - w->pos) { w->err = 1; return; }
    w->pos += (size_t)n;
}

const char *routing_pf_mode_name(routing_pf_mode_t mode) {
    switch (mode) {
        case ROUTING_PF_ROUTE_TO_LO0:       return "route-to-lo0";
        case ROUTING_PF_ROUTE_TO_LO0_NOGW:  return "route-to-lo0-nogw";
        case ROUTING_PF_DIVERT_TO:          return "divert-to";
        case ROUTING_PF_DIVERT_TO_OLD:      return "divert-to-old";
        case ROUTING_PF_RDR_TO:             return "rdr-to";
        case ROUTING_PF_RDR_TO_OLD:         return "rdr-to-old";
        case ROUTING_PF_LEGACY_RDR:         return "legacy-rdr";
        case ROUTING_PF_COMPAT_RDR:         return "compat-rdr";
    }
    return "unknown";
}

static int rule_is_ipv4(const rule_t *rule, rule_action_t action) {
    return rule && rule->type == RULE_TYPE_IP_CIDR &&
           rule->action == action && rule->address_len == 4;
}

static int cidr_overlaps(const rule_t *left, const rule_t *right) {
    unsigned bits = left->prefix < right->prefix ? left->prefix : right->prefix;
    unsigned whole = bits / 8;
    unsigned tail = bits % 8;
    if (whole && memcmp(left->address, right->address, whole) != 0) return 0;
    if (!tail) return 1;
    uint8_t mask = (uint8_t)(0xffu << (8 - tail));
    return (left->address[whole] & mask) == (right->address[whole] & mask);
}

static int direct_conflicts_with_block(const ruleset_t *rules, size_t index) {
    const rule_t *direct = &rules->entries[index];
    for (size_t i = 0; i < rules->count; ++i) {
        const rule_t *block = &rules->entries[i];
        if (rule_is_ipv4(block, RULE_ACTION_BLOCK) && cidr_overlaps(direct, block))
            return 1;
    }
    return 0;
}

static int rules_have_ipv4(const ruleset_t *rules, rule_action_t action) {
    if (!rules) return 0;
    for (size_t i = 0; i < rules->count; ++i)
        if (rule_is_ipv4(&rules->entries[i], action)) return 1;
    return 0;
}

static void pf_rule_cidrs(pf_w_t *w, const ruleset_t *rules,
                          rule_action_t action, int prepend_comma) {
    int emitted = 0;
    if (!rules) return;
    for (size_t i = 0; i < rules->count; ++i) {
        if (!rule_is_ipv4(&rules->entries[i], action)) continue;
        /* overlapping cidrs stay in the tunnel because one pf address cannot
           carry both verdicts without making rule order part of policy */
        if (action == RULE_ACTION_DIRECT && direct_conflicts_with_block(rules, i))
            continue;
        pf_ap(w, "%s%s", prepend_comma || emitted ? ", " : "",
              rules->entries[i].value);
        emitted = 1;
    }
}

static void pf_bypass_table(pf_w_t *w, const char *server_ips,
                            const ruleset_t *rules) {
    pf_ap(w, "table <senko_bypass> persist { 127.0.0.0/8, 10.0.0.0/8, "
             "172.16.0.0/12, 192.168.0.0/16, 224.0.0.0/4, 255.255.255.255/32, "
             "%s", server_ips);
    pf_rule_cidrs(w, rules, RULE_ACTION_DIRECT, 1);
    pf_ap(w, " }\n");

    pf_ap(w, "table <senko_block> persist");
    if (rules_have_ipv4(rules, RULE_ACTION_BLOCK)) {
        pf_ap(w, " { ");
        pf_rule_cidrs(w, rules, RULE_ACTION_BLOCK, 0);
        pf_ap(w, " }");
    }
    pf_ap(w, "\n");
}

static void pf_direct_bypass_set(pf_w_t *w, const char *server_ips,
                                 const ruleset_t *rules) {
    pf_ap(w, "{ 127.0.0.0/8, 10.0.0.0/8, 172.16.0.0/12, "
             "192.168.0.0/16, 224.0.0.0/4, 255.255.255.255/32, %s",
          server_ips);
    pf_rule_cidrs(w, rules, RULE_ACTION_DIRECT, 1);
    pf_ap(w, " }");
}

static void pf_ip_blocks(pf_w_t *w, const ruleset_t *rules,
                         const char ifnames[][32], size_t if_count,
                         int table) {
    if (!rules_have_ipv4(rules, RULE_ACTION_BLOCK)) return;
    for (size_t i = 0; i < if_count; ++i) {
        pf_ap(w, "block return out quick on %s inet from any to ", ifnames[i]);
        if (table) {
            pf_ap(w, "<senko_block>");
        } else {
            pf_ap(w, "{ ");
            pf_rule_cidrs(w, rules, RULE_ACTION_BLOCK, 0);
            pf_ap(w, " }");
        }
        pf_ap(w, "\n");
    }
}

/* the trailing block-udp443 / block-inet6 / pass-out triplet per interface, shared by several modes */
static void pf_block_triplet(pf_w_t *w, const char *ifn) {
    pf_ap(w, "pass in quick on %s inet from <senko_bypass> to any keep state\n"
             "pass out quick on %s inet from any to <senko_bypass> keep state\n"
             "block return out quick on %s inet proto udp from any to ! <senko_bypass> port 443\n"
             "block return out quick on %s inet6 all\n"
             "pass out on %s all keep state\n",
          ifn, ifn, ifn, ifn, ifn);
}

static routing_status_t routing_pf_conf_build(const char *server_ips,
                                              const ruleset_t *rules,
                                              const char ifnames[][32], size_t if_count,
                                              int redir_port, int dns_local_port,
                                              routing_pf_mode_t mode, int anchored,
                                              char *buf, size_t cap, size_t *out_len) {
    if (!server_ips || !*server_ips || !ifnames || if_count == 0 || !buf
        || dns_local_port <= 0)
        return ROUTING_ERR_ARG;

    pf_w_t w = { buf, cap, 0, 0 };

    if (!anchored) {
        /* ios 7 pf retains idle tcp states long enough to exhaust its small table */
        pf_ap(&w, "set timeout { tcp.first 30, tcp.opening 30, tcp.established 7200, "
                  "tcp.closing 30, tcp.finwait 30, udp.first 30, udp.single 30, "
                  "udp.multiple 60, icmp.first 10, other.first 30, frag 30 }\n");
    }

    switch (mode) {
        case ROUTING_PF_ROUTE_TO_LO0:
        case ROUTING_PF_ROUTE_TO_LO0_NOGW: {
            pf_bypass_table(&w, server_ips, rules);
            pf_ip_blocks(&w, rules, ifnames, if_count, 1);
            for (size_t i = 0; i < if_count; i++)
                pf_ap(&w, "nat on %s inet proto tcp from any to ! <senko_bypass> -> 127.0.0.1\n",
                      ifnames[i]);
            pf_ap(&w, "rdr pass on lo0 inet proto tcp from any to ! <senko_bypass> -> 127.0.0.1 port %d\n",
                  redir_port);
            for (size_t i = 0; i < if_count; i++)
                pf_ap(&w, "rdr pass on %s proto udp from any to any port 53 -> 127.0.0.1 port %d\n",
                      ifnames[i], dns_local_port);
            for (size_t i = 0; i < if_count; i++) {
                const char *ifn = ifnames[i];
                pf_ap(&w, "pass out quick on %s inet proto tcp from any to <senko_bypass> flags S/SA keep state\n"
                          "pass out quick on %s route-to lo0 inet proto tcp from any to ! <senko_bypass> flags S/SA keep state\n",
                      ifn, ifn);
                pf_block_triplet(&w, ifn);
            }
            pf_ap(&w, "pass in all keep state\n");
            break;
        }

        case ROUTING_PF_DIVERT_TO: {
            pf_ap(&w, "set skip on lo0\n");
            pf_bypass_table(&w, server_ips, rules);
            pf_ip_blocks(&w, rules, ifnames, if_count, 1);
            for (size_t i = 0; i < if_count; i++) {
                const char *ifn = ifnames[i];
                pf_ap(&w, "pass out quick on %s inet proto tcp from any to <senko_bypass> flags S/SA keep state\n"
                          "pass out quick on %s divert-to 127.0.0.1 port %d inet proto tcp from any to ! <senko_bypass> flags S/SA keep state\n"
                          "pass out quick on %s proto udp from any to any port 53 rdr-to 127.0.0.1 port %d\n",
                      ifn, ifn, redir_port, ifn, dns_local_port);
                pf_block_triplet(&w, ifn);
            }
            pf_ap(&w, "pass in all keep state\n");
            break;
        }

        case ROUTING_PF_DIVERT_TO_OLD: {
            pf_ap(&w, "set skip on lo0\n");
            pf_bypass_table(&w, server_ips, rules);
            pf_ip_blocks(&w, rules, ifnames, if_count, 1);
            for (size_t i = 0; i < if_count; i++) {
                const char *ifn = ifnames[i];
                pf_ap(&w, "pass out quick on %s inet proto tcp from any to <senko_bypass> keep state\n"
                          "pass out quick on %s inet proto tcp from any to ! <senko_bypass> divert-to 127.0.0.1 port %d keep state\n"
                          "pass out quick on %s proto udp from any to any port 53 rdr-to 127.0.0.1 port %d\n",
                      ifn, ifn, redir_port, ifn, dns_local_port);
                pf_block_triplet(&w, ifn);
            }
            pf_ap(&w, "pass in all keep state\n");
            break;
        }

        case ROUTING_PF_RDR_TO: {
            pf_ap(&w, "set skip on lo0\n");
            pf_bypass_table(&w, server_ips, rules);
            pf_ip_blocks(&w, rules, ifnames, if_count, 1);
            for (size_t i = 0; i < if_count; i++) {
                const char *ifn = ifnames[i];
                pf_ap(&w, "pass out quick on %s inet proto tcp from any to <senko_bypass> flags S/SA keep state\n"
                          "pass out quick on %s inet proto tcp from any to ! <senko_bypass> rdr-to 127.0.0.1 port %d flags S/SA keep state\n"
                          "pass out quick on %s proto udp from any to any port 53 rdr-to 127.0.0.1 port %d\n",
                      ifn, ifn, redir_port, ifn, dns_local_port);
                pf_block_triplet(&w, ifn);
            }
            pf_ap(&w, "pass in all keep state\n");
            break;
        }

        case ROUTING_PF_RDR_TO_OLD: {
            pf_ap(&w, "set skip on lo0\n");
            pf_bypass_table(&w, server_ips, rules);
            pf_ip_blocks(&w, rules, ifnames, if_count, 1);
            for (size_t i = 0; i < if_count; i++) {
                const char *ifn = ifnames[i];
                pf_ap(&w, "pass out quick on %s inet proto tcp from any to <senko_bypass> keep state\n"
                          "pass out quick on %s inet proto tcp from any to ! <senko_bypass> rdr-to 127.0.0.1 port %d keep state\n"
                          "pass out quick on %s proto udp from any to any port 53 rdr-to 127.0.0.1 port %d\n",
                      ifn, ifn, redir_port, ifn, dns_local_port);
                pf_block_triplet(&w, ifn);
            }
            pf_ap(&w, "pass in all keep state\n");
            break;
        }

        case ROUTING_PF_LEGACY_RDR: {
            pf_ap(&w, "set skip on lo0\n");
            pf_bypass_table(&w, server_ips, rules);
            pf_ip_blocks(&w, rules, ifnames, if_count, 1);
            for (size_t i = 0; i < if_count; i++) {
                pf_ap(&w, "rdr pass on %s inet proto tcp from any to ! <senko_bypass> -> 127.0.0.1 port %d\n",
                      ifnames[i], redir_port);
                pf_ap(&w, "rdr pass on %s proto udp from any to any port 53 -> 127.0.0.1 port %d\n",
                      ifnames[i], dns_local_port);
            }
            for (size_t i = 0; i < if_count; i++)
                pf_block_triplet(&w, ifnames[i]);
            pf_ap(&w, "pass in all keep state\n");
            break;
        }

        case ROUTING_PF_COMPAT_RDR: {
            pf_ip_blocks(&w, rules, ifnames, if_count, 0);
            for (size_t i = 0; i < if_count; i++) {
                const char *ifn = ifnames[i];
                pf_ap(&w, "rdr pass on %s inet proto tcp from any to ! ", ifn);
                pf_direct_bypass_set(&w, server_ips, rules);
                pf_ap(&w, " -> 127.0.0.1 port %d\n", redir_port);
                pf_ap(&w, "rdr pass on %s inet proto udp from any to any port 53 -> 127.0.0.1 port %d\n",
                      ifn, dns_local_port);
                pf_ap(&w, "pass out on %s all\n", ifn);
            }
            break;
        }

        default:
            return ROUTING_ERR_ARG;
    }

    if (w.err) return ROUTING_ERR_SPACE;
    if (out_len) *out_len = w.pos;
    return ROUTING_OK;
}

routing_status_t routing_pf_conf(const char *server_ips,
                                 const char ifnames[][32], size_t if_count,
                                 int redir_port, int dns_local_port,
                                 routing_pf_mode_t mode,
                                 char *buf, size_t cap, size_t *out_len) {
    return routing_pf_conf_build(server_ips, NULL, ifnames, if_count,
                                 redir_port, dns_local_port, mode, 0,
                                 buf, cap, out_len);
}

routing_status_t routing_pf_conf_rules(const char *server_ips,
                                       const ruleset_t *rules,
                                       const char ifnames[][32], size_t if_count,
                                       int redir_port, int dns_local_port,
                                       routing_pf_mode_t mode,
                                       char *buf, size_t cap, size_t *out_len) {
    return routing_pf_conf_build(server_ips, rules, ifnames, if_count,
                                 redir_port, dns_local_port, mode, 0,
                                 buf, cap, out_len);
}

routing_status_t routing_pf_anchor_conf(const char *server_ips,
                                        const char ifnames[][32], size_t if_count,
                                        int redir_port, int dns_local_port,
                                        routing_pf_mode_t mode,
                                        char *buf, size_t cap, size_t *out_len) {
    return routing_pf_conf_build(server_ips, NULL, ifnames, if_count,
                                 redir_port, dns_local_port, mode, 1,
                                 buf, cap, out_len);
}

routing_status_t routing_pf_anchor_conf_rules(const char *server_ips,
                                              const ruleset_t *rules,
                                              const char ifnames[][32], size_t if_count,
                                              int redir_port, int dns_local_port,
                                              routing_pf_mode_t mode,
                                              char *buf, size_t cap, size_t *out_len) {
    return routing_pf_conf_build(server_ips, rules, ifnames, if_count,
                                 redir_port, dns_local_port, mode, 1,
                                 buf, cap, out_len);
}
