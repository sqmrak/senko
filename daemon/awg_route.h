#ifndef AWG_ROUTE_H
#define AWG_ROUTE_H

#include "core/awg_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ifname[32];
    char endpoint[64];
    char gateway[64];
    char ipv4[64];
    char ipv6[64];
    uint16_t mtu;
    int has_ipv4;
    int has_ipv6;
} awg_route_plan_t;

/* share address extraction between setup and non-mutating interface probes */
int awg_route_plan_for_interface(const awg_config_t *cfg, const char *ifname,
                                 awg_route_plan_t *plan);

/* preserve the endpoint route before redirecting the default address space */
int awg_route_plan_build(const awg_config_t *cfg, const char *ifname,
                         const char *endpoint, const char *gateway,
                         awg_route_plan_t *plan);

/* apply in endpoint-first order so the udp socket never loops into utun */
int awg_route_plan_up(const awg_route_plan_t *plan);

/* remove only routes created by this plan before taking the interface down */
void awg_route_plan_down(const awg_route_plan_t *plan);

/* query the physical gateway before split routes change route lookup */
int awg_route_gateway_for_endpoint(const char *endpoint, char *gateway, size_t gateway_cap);

/* configure a fresh utun without touching the routing table */
int awg_route_interface_up(const awg_route_plan_t *plan);

#ifdef __cplusplus
}
#endif

#endif
