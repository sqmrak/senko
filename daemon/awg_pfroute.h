#ifndef AWG_PFROUTE_H
#define AWG_PFROUTE_H

#include <stddef.h>

/* probe the kernel route socket before any mutating route message is enabled */
int awg_pfroute_probe_get4(const char *destination, char *detail, size_t detail_cap);
int awg_pfroute_probe_host4(const char *destination, const char *gateway, char *detail, size_t detail_cap);
int awg_pfroute_gateway4(const char *destination, char *gateway, size_t gateway_cap);
int awg_pfroute_host4(int add, const char *destination, const char *gateway);
int awg_pfroute_net4_if(int add, const char *network, const char *netmask, const char *ifname);
int awg_pfroute_net6_if(int add, const char *network, const char *netmask, const char *ifname);

#endif
