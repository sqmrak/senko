#ifndef AWG_UTUN_H
#define AWG_UTUN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* open utun directly because old ios has no network extension runtime */
int awg_utun_open(char *ifname, size_t ifname_cap);

/* bind the utun interface to the physical interface carrying the tunnel
   (XNU UTUN_OPT_SET_DELEGATE_INTERFACE) so iOS keeps Wi-Fi / NWPath state */
int awg_utun_set_delegate(int fd, const char *physical_ifname);

#ifdef __cplusplus
}
#endif

#endif
