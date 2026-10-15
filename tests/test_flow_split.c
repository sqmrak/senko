#include "flow_split.h"
#include "packet_fixture.h"

#include <stdio.h>
#include <string.h>

static int failures;
static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

static const uint8_t APP4[4] = { 10, 8, 0, 2 };
static const uint8_t SITE4[4] = { 93, 184, 216, 34 };
static const uint8_t APP6[16] = { 0xfd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2 };
static const uint8_t SITE6[16] = { 0x20, 1, 0xdb, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7 };

typedef struct {
    rule_action_t answer;
    int calls;
    tun_flow_key_t last;
} decider_t;

static rule_action_t decide(void *ctx, const tun_flow_key_t *key) {
    decider_t *d = ctx;
    ++d->calls;
    d->last = *key;
    return d->answer;
}

static flow_split_route_t route(flow_split_t *split, const uint8_t *p, size_t len,
                                uint64_t now, decider_t *d) {
    return flow_split_route(split, p, len, now, decide, d);
}

static void keys(void) {
    uint8_t p[256];
    tun_flow_key_t key;
    int opens = 0;
    size_t l4 = 0;
    size_t len = pkt_tcp4(p, APP4, 40001, SITE4, 443, PKT_SYN, 1, 0, 0);
    ok("a tcp syn is a flow", flow_split_key(p, len, &key, &opens, &l4) == 1);
    ok("it opens the flow", opens == 1);
    ok("the header starts after the ip header", l4 == 20);
    ok("the key names both ends", key.protocol == TUN_FLOW_TCP && key.address_len == 4 &&
       key.source_port == 40001 && key.destination_port == 443 &&
       memcmp(key.source, APP4, 4) == 0 && memcmp(key.destination, SITE4, 4) == 0);

    len = pkt_tcp4(p, APP4, 40001, SITE4, 443, PKT_SYN | PKT_ACK, 1, 1, 0);
    ok("a syn-ack does not open", flow_split_key(p, len, &key, &opens, NULL) == 1 && opens == 0);
    len = pkt_tcp4(p, APP4, 40001, SITE4, 443, PKT_ACK, 1, 1, 10);
    ok("data does not open", flow_split_key(p, len, &key, &opens, NULL) == 1 && opens == 0);

    len = pkt_udp4(p, APP4, 5000, SITE4, 443, (const uint8_t *)"hello", 5);
    ok("a udp datagram is a flow", flow_split_key(p, len, &key, &opens, NULL) == 1 &&
       key.protocol == TUN_FLOW_UDP && opens == 0);

    len = pkt_tcp6(p, APP6, 40002, SITE6, 443, PKT_SYN, 1, 0, 0, 0, 0);
    ok("an ipv6 tcp syn is a flow", flow_split_key(p, len, &key, &opens, &l4) == 1 &&
       key.address_len == 16 && opens == 1 && l4 == 40 && memcmp(key.destination, SITE6, 16) == 0);
    len = pkt_udp6(p, APP6, 5001, SITE6, 443, (const uint8_t *)"hi", 2);
    ok("an ipv6 udp datagram is a flow", flow_split_key(p, len, &key, &opens, NULL) == 1);
    len = pkt_tcp6(p, APP6, 40002, SITE6, 443, PKT_SYN, 1, 0, 0, 1, 60);
    ok("an extension header leaves no ports to read", flow_split_key(p, len, &key, &opens, NULL) == 0);

    len = pkt_echo4(p, APP4, SITE4, 1);
    ok("icmp is not a flow", flow_split_key(p, len, &key, &opens, NULL) == 0);

    len = pkt_udp4(p, APP4, 5000, SITE4, 443, (const uint8_t *)"hello", 5);
    p[6] = 0x20; /* more fragments */
    ok("a first fragment is not a flow", flow_split_key(p, len, &key, &opens, NULL) == 0);
    p[6] = 0x00;
    p[7] = 0x10; /* a later fragment */
    ok("a later fragment is not a flow", flow_split_key(p, len, &key, &opens, NULL) == 0);

    len = pkt_tcp4(p, APP4, 40001, SITE4, 443, PKT_SYN, 1, 0, 0);
    ok("a cut tcp header is refused", flow_split_key(p, 30, &key, &opens, NULL) == 0);
    ok("an empty packet is refused", flow_split_key(p, 0, &key, &opens, NULL) == 0);
    pkt_wr16(p + 2, 400);
    ok("a length field past the packet is refused", flow_split_key(p, len, &key, &opens, NULL) == 0);
    p[0] = 0x4f;
    ok("a header longer than the packet is refused", flow_split_key(p, 24, &key, &opens, NULL) == 0);
}

static void tcp_flows(void) {
    flow_split_t split;
    decider_t d;
    memset(&d, 0, sizeof d);
    d.answer = RULE_ACTION_DIRECT;
    uint8_t syn[256], ack[256], other[256];
    flow_split_init(&split);
    size_t syn_len = pkt_tcp4(syn, APP4, 40001, SITE4, 443, PKT_SYN, 1, 0, 0);
    size_t ack_len = pkt_tcp4(ack, APP4, 40001, SITE4, 443, PKT_ACK, 2, 2, 100);

    ok("a direct syn goes to the local stack", route(&split, syn, syn_len, 1000, &d) == FLOW_SPLIT_LOCAL);
    ok("the rule was asked once", d.calls == 1 && d.last.destination_port == 443);
    ok("its data follows without asking again", route(&split, ack, ack_len, 1100, &d) == FLOW_SPLIT_LOCAL &&
       d.calls == 1);
    ok("the flow is counted once", split.local_flows == 1 && split.tunnel_flows == 0);

    size_t other_len = pkt_tcp4(other, APP4, 40009, SITE4, 443, PKT_ACK, 2, 2, 100);
    ok("a flow the table never saw stays in the tunnel", route(&split, other, other_len, 1200, &d) ==
       FLOW_SPLIT_TUNNEL && d.calls == 1);

    d.answer = RULE_ACTION_PROXY;
    ok("a new syn on the same tuple is decided again", route(&split, syn, syn_len, 1300, &d) ==
       FLOW_SPLIT_TUNNEL && d.calls == 2);
    ok("and the old answer is gone", route(&split, ack, ack_len, 1400, &d) == FLOW_SPLIT_TUNNEL &&
       d.calls == 2);

    d.answer = RULE_ACTION_BLOCK;
    ok("a blocked syn goes to the stack, which resets it", route(&split, syn, syn_len, 1500, &d) ==
       FLOW_SPLIT_LOCAL);

    d.answer = RULE_ACTION_DIRECT;
    ok("a flow idle for an hour is forgotten", route(&split, ack, ack_len,
       1500 + FLOW_SPLIT_TCP_IDLE_MS + 1, &d) == FLOW_SPLIT_TUNNEL);
}

static void udp_flows(void) {
    flow_split_t split;
    decider_t d;
    memset(&d, 0, sizeof d);
    d.answer = RULE_ACTION_DIRECT;
    uint8_t a[256], b[256], dns[256];
    flow_split_init(&split);
    size_t a_len = pkt_udp4(a, APP4, 5000, SITE4, 443, (const uint8_t *)"quic", 4);
    size_t b_len = pkt_udp4(b, APP4, 5001, SITE4, 3478, (const uint8_t *)"stun", 4);
    size_t dns_len = pkt_udp4(dns, APP4, 5002, SITE4, 53, (const uint8_t *)"q", 1);

    ok("a direct datagram goes to the stack", route(&split, a, a_len, 100, &d) == FLOW_SPLIT_LOCAL);
    ok("the next one costs a lookup in the table", route(&split, a, a_len, 200, &d) == FLOW_SPLIT_LOCAL &&
       d.calls == 1);
    d.answer = RULE_ACTION_PROXY;
    ok("a tunnel datagram is remembered too", route(&split, b, b_len, 300, &d) == FLOW_SPLIT_TUNNEL &&
       route(&split, b, b_len, 400, &d) == FLOW_SPLIT_TUNNEL && d.calls == 2);
    d.answer = RULE_ACTION_DIRECT;
    ok("a dns query never leaves the tunnel", route(&split, dns, dns_len, 500, &d) == FLOW_SPLIT_TUNNEL &&
       d.calls == 2);
    ok("an idle association is decided again", route(&split, a, a_len,
       200 + FLOW_SPLIT_UDP_IDLE_MS + 1, &d) == FLOW_SPLIT_LOCAL && d.calls == 3);

    uint8_t frag[256];
    size_t frag_len = pkt_udp4(frag, APP4, 5003, SITE4, 443, (const uint8_t *)"data", 4);
    frag[6] = 0x20;
    ok("a fragment follows the tunnel without a lookup", route(&split, frag, frag_len, 600, &d) ==
       FLOW_SPLIT_TUNNEL && d.calls == 3);
    ok("junk follows the tunnel", route(&split, a, 3, 700, &d) == FLOW_SPLIT_TUNNEL && d.calls == 3);
}

static void full_table(void) {
    flow_split_t split;
    decider_t d;
    memset(&d, 0, sizeof d);
    d.answer = RULE_ACTION_DIRECT;
    uint8_t p[256];
    flow_split_init(&split);
    for (unsigned i = 0; i < FLOW_SPLIT_TABLE_MAX; ++i) {
        size_t len = pkt_udp4(p, APP4, (uint16_t)(6000 + i), SITE4, 443, (const uint8_t *)"x", 1);
        route(&split, p, len, 1000 + i, &d);
    }
    ok("a full table has dropped nothing yet", split.evicted == 0 && d.calls == FLOW_SPLIT_TABLE_MAX);
    size_t len = pkt_udp4(p, APP4, 7000, SITE4, 443, (const uint8_t *)"x", 1);
    route(&split, p, len, 5000, &d);
    ok("one more drops the longest idle flow and says so", split.evicted == 1);
    len = pkt_udp4(p, APP4, 6000, SITE4, 443, (const uint8_t *)"x", 1);
    int before = d.calls;
    route(&split, p, len, 5100, &d);
    ok("the dropped flow is decided again", d.calls == before + 1);
    len = pkt_udp4(p, APP4, 6200, SITE4, 443, (const uint8_t *)"x", 1);
    before = d.calls;
    route(&split, p, len, 5200, &d);
    ok("a flow that stayed keeps its answer", d.calls == before);
}

int main(void) {
    keys();
    tcp_flows();
    udp_flows();
    full_table();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("flow split ok\n");
    return 0;
}
