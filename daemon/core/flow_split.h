#ifndef SENKO_FLOW_SPLIT_H
#define SENKO_FLOW_SPLIT_H

#include <stddef.h>
#include <stdint.h>

#include "rules.h"
#include "tun_flow.h"

#ifdef __cplusplus
extern "C" {
#endif

/* which side of a packet tunnel a packet from the app takes.

   amneziawg moves whole ip packets, so a flow the rules send direct cannot be
   left out by a socket choice the way the stream relays do it. each packet is
   sorted here instead: the tunnel keeps the ones its server should carry, and
   the local stack gets the ones that terminate on this device and leave by
   the physical interface (direct and blocked flows alike, the stack's relays
   tell them apart).

   a flow is decided once, on its first packet (the syn of a tcp flow), and the
   answer is remembered so the rest of its packets cost a table lookup and not
   a process table scan. packets the table does not know follow the tunnel,
   which is where an unmanaged flow was before there were rules.

   fragments and extension headers carry no ports, so they always follow the
   tunnel: sending half of a datagram each way would deliver neither half */

#define FLOW_SPLIT_TABLE_MAX 256
#define FLOW_SPLIT_TCP_IDLE_MS (60u * 60u * 1000u)
#define FLOW_SPLIT_UDP_IDLE_MS (60u * 1000u)

typedef enum {
    FLOW_SPLIT_TUNNEL = 0,
    FLOW_SPLIT_LOCAL
} flow_split_route_t;

/* the verdict for a new flow; it runs on the caller's thread and may block
   briefly, like the owner lookup behind it */
typedef rule_action_t (*flow_split_decide_fn)(void *ctx, const tun_flow_key_t *key);

typedef struct {
    tun_flow_key_t key;
    uint64_t last_ms;
    uint8_t  route; /* flow_split_route_t */
    uint8_t  used;
} flow_split_entry_t;

typedef struct {
    flow_split_entry_t entries[FLOW_SPLIT_TABLE_MAX];
    uint64_t tunnel_flows;
    uint64_t local_flows;
    uint64_t evicted; /* a full table forgot a live flow; counted, never silent */
} flow_split_t;

void flow_split_init(flow_split_t *split);

/* the flow an ip packet belongs to. 1 with key filled for an unfragmented tcp
   or udp packet, 0 for everything else. *opens is set for a tcp syn that does
   not acknowledge, the only packet that starts a tcp flow. *l4_offset, when
   asked for, is where the tcp or udp header starts inside the packet */
int flow_split_key(const uint8_t *packet, size_t len, tun_flow_key_t *key, int *opens,
                   size_t *l4_offset);

flow_split_route_t flow_split_route(flow_split_t *split, const uint8_t *packet, size_t len,
                                    uint64_t now_ms, flow_split_decide_fn decide, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
