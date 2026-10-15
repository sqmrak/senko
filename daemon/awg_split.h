#ifndef SENKO_AWG_SPLIT_H
#define SENKO_AWG_SPLIT_H

#include <stddef.h>
#include <stdint.h>

#include "awg_link.h"
#include "core/rules.h"
#include "core/tun_policy.h"
#include "direct_socket.h"
#include "utun_device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* amneziawg with routing rules. the plain link (awg_link.h) sends every packet
   to the server; this one sorts them first. the packets of flows the rules keep
   in the tunnel are sealed as before, the others go to the local ip stack and
   its relays (tun_tcp, tun_udp), which hold the app's connection and open their
   own socket bound to the physical interface, as the vless tunnel does for a
   direct flow. one thread runs the device, the stack, the relays and the link.

   what the rules can decide here: app, ip and geoip rules for any flow, and
   domain rules for flows whose address a dns answer from the server named.
   dns itself always stays in the tunnel. a domain rule that blocks has nothing
   to act on, because the answer is already in the tunnel's hands: awg_split_
   unenforced counts them so the caller can say so */

typedef struct awg_split awg_split_t;

typedef struct {
    awg_link_t *link;             /* borrowed, already initialised */
    utun_device_t *device;        /* borrowed, open; closed with the split tunnel */
    ruleset_t *rules;             /* borrowed until awg_split_close */
    tun_policy_app_fn app_of;     /* NULL when app rules cannot be decided */
    void *app_ctx;
    direct_socket_iface_t direct; /* where direct flows leave */
    int wake_fd;                  /* readable when the owner wants the run to end */
    uint16_t mtu;                 /* the tunnel interface's, which the stack must not exceed */
    uint8_t dns[4];               /* an address the relays name for a lookup they refuse */
} awg_split_config_t;

typedef struct {
    uint32_t tunnel_flows; /* flows the rules left to the server */
    uint32_t local_flows;  /* flows sent to the local stack */
    uint32_t forgotten;    /* live flows a full table dropped */
    uint32_t tcp_refused;  /* direct tcp flows answered with a reset before they opened */
    uint32_t tcp_aborted;  /* direct tcp flows that failed after they opened */
} awg_split_counts_t;

/* 1 when the rules send something other than the tunnel, so sorting is needed */
int awg_split_wanted(const ruleset_t *rules);

/* rules that block a domain: they cannot act on a tunnel that carries the lookup */
size_t awg_split_unenforced(const ruleset_t *rules);

awg_split_t *awg_split_open(const awg_split_config_t *config, char *reason, size_t reason_cap);

/* runs until the owner asks through wake_fd or the link or the device fails;
   the status and reason are those of awg_link_run */
awg_link_status_t awg_split_run(awg_split_t *split, char *reason, size_t reason_cap);

void awg_split_counts(const awg_split_t *split, awg_split_counts_t *out);

/* ends the loop and closes the device, the stack and the relays */
void awg_split_close(awg_split_t *split);

#ifdef __cplusplus
}
#endif

#endif
