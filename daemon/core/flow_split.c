#include "flow_split.h"

#include <string.h>

#define PROTO_TCP 6
#define PROTO_UDP 17
#define TCP_HEADER_MIN 20
#define UDP_HEADER 8
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_ACK 0x10

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

void flow_split_init(flow_split_t *split) {
    if (split) memset(split, 0, sizeof *split);
}

int flow_split_key(const uint8_t *packet, size_t len, tun_flow_key_t *key, int *opens,
                   size_t *l4_offset) {
    size_t l4;
    size_t addr_len;
    uint8_t proto;
    if (opens) *opens = 0;
    if (l4_offset) *l4_offset = 0;
    if (!packet || !key || len < 1) return 0;
    if ((packet[0] >> 4) == 4) {
        size_t header = (size_t)(packet[0] & 0x0f) * 4;
        if (len < 20 || header < 20 || header > len) return 0;
        /* more fragments set, or a nonzero offset: no ports to read */
        if (be16(packet + 6) & 0x3fff) return 0;
        if (be16(packet + 2) > len) return 0;
        proto = packet[9];
        l4 = header;
        addr_len = 4;
        if (be16(packet + 2) < l4) return 0;
        len = be16(packet + 2);
    } else if ((packet[0] >> 4) == 6) {
        if (len < 40) return 0;
        proto = packet[6];
        l4 = 40;
        addr_len = 16;
    } else {
        return 0;
    }
    if (proto != PROTO_TCP && proto != PROTO_UDP) return 0;
    if (len - l4 < (proto == PROTO_TCP ? (size_t)TCP_HEADER_MIN : (size_t)UDP_HEADER)) return 0;

    memset(key, 0, sizeof *key);
    key->address_len = (uint8_t)addr_len;
    key->protocol = proto == PROTO_TCP ? TUN_FLOW_TCP : TUN_FLOW_UDP;
    memcpy(key->source, packet + (addr_len == 4 ? 12 : 8), addr_len);
    memcpy(key->destination, packet + (addr_len == 4 ? 16 : 24), addr_len);
    key->source_port = be16(packet + l4);
    key->destination_port = be16(packet + l4 + 2);
    if (proto == PROTO_TCP && opens)
        *opens = (packet[l4 + 13] & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == TCP_FLAG_SYN;
    if (l4_offset) *l4_offset = l4;
    return 1;
}

static int same_flow(const tun_flow_key_t *a, const tun_flow_key_t *b) {
    return a->protocol == b->protocol && a->address_len == b->address_len &&
           a->source_port == b->source_port && a->destination_port == b->destination_port &&
           memcmp(a->source, b->source, a->address_len) == 0 &&
           memcmp(a->destination, b->destination, a->address_len) == 0;
}

static uint64_t idle_limit(const tun_flow_key_t *key) {
    return key->protocol == TUN_FLOW_TCP ? FLOW_SPLIT_TCP_IDLE_MS : FLOW_SPLIT_UDP_IDLE_MS;
}

static int expired(const flow_split_entry_t *entry, uint64_t now_ms) {
    return now_ms >= entry->last_ms && now_ms - entry->last_ms > idle_limit(&entry->key);
}

static flow_split_entry_t *find(flow_split_t *split, const tun_flow_key_t *key) {
    for (size_t i = 0; i < FLOW_SPLIT_TABLE_MAX; ++i) {
        flow_split_entry_t *entry = &split->entries[i];
        if (entry->used && same_flow(&entry->key, key)) return entry;
    }
    return NULL;
}

static flow_split_entry_t *slot_for(flow_split_t *split, uint64_t now_ms) {
    flow_split_entry_t *oldest = &split->entries[0];
    for (size_t i = 0; i < FLOW_SPLIT_TABLE_MAX; ++i) {
        flow_split_entry_t *entry = &split->entries[i];
        if (!entry->used || expired(entry, now_ms)) return entry;
        if (entry->last_ms < oldest->last_ms) oldest = entry;
    }
    ++split->evicted;
    return oldest;
}

flow_split_route_t flow_split_route(flow_split_t *split, const uint8_t *packet, size_t len,
                                    uint64_t now_ms, flow_split_decide_fn decide, void *ctx) {
    tun_flow_key_t key;
    int opens = 0;
    if (!split || !decide || !flow_split_key(packet, len, &key, &opens, NULL)) return FLOW_SPLIT_TUNNEL;
    /* lookups belong to the resolver the tunnel's dns entry points at */
    if (key.destination_port == 53) return FLOW_SPLIT_TUNNEL;

    flow_split_entry_t *known = find(split, &key);
    if (known && expired(known, now_ms)) {
        known->used = 0;
        known = NULL;
    }
    if (known && !opens) {
        known->last_ms = now_ms;
        return (flow_split_route_t)known->route;
    }
    /* a tcp segment of a flow that began before the tunnel, or that the table
       forgot, stays where its connection already is */
    if (!known && key.protocol == TUN_FLOW_TCP && !opens) return FLOW_SPLIT_TUNNEL;

    flow_split_route_t route = decide(ctx, &key) == RULE_ACTION_PROXY ? FLOW_SPLIT_TUNNEL
                                                                      : FLOW_SPLIT_LOCAL;
    if (route == FLOW_SPLIT_LOCAL) ++split->local_flows;
    else ++split->tunnel_flows;

    /* tcp remembers only the flows that leave the tunnel; the rest is the default */
    if (key.protocol == TUN_FLOW_TCP && route == FLOW_SPLIT_TUNNEL) {
        if (known) known->used = 0;
        return route;
    }
    flow_split_entry_t *entry = known ? known : slot_for(split, now_ms);
    entry->key = key;
    entry->route = (uint8_t)route;
    entry->last_ms = now_ms;
    entry->used = 1;
    return route;
}
