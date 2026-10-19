#ifndef SENKO_EGRESS_H
#define SENKO_EGRESS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the physical interface traffic leaves by and its ipv4 address: wifi first,
   then cellular, the order the kernel installs the default route in. probes
   bind to it so they measure the network, not the tunnel. 0 when found */
int egress_snapshot(char *name, size_t name_cap, char *ip, size_t ip_cap);

/* the same interface choice, also counting one that only has a global ipv6
   address: an ipv6-only cellular network has no ipv4 for the call above to
   find. for dials that need the interface and not its address. 0 when found */
int egress_interface(char *name, size_t name_cap);

#ifdef __cplusplus
}
#endif

#endif
