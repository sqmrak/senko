#ifndef AWG_UTUN_H
#define AWG_UTUN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* open utun directly because old ios has no network extension runtime */
int awg_utun_open(char *ifname, size_t ifname_cap);

/* bind the utun to the physical interface that carries the tunnel, so ios 13
   and later still count the link as wifi or cellular and keep its status bar
   glyph. NULL or "" clears the delegate while no physical link is up.
   fails with EINVAL on a name that does not fit IFNAMSIZ */
int awg_utun_set_delegate(int fd, const char *physical_ifname);

/* picks the physical link the tunnel rides: en0 with a usable address first,
   then the first pdp_ip*, else "". returns -1 only when the interface list
   cannot be read */
int awg_utun_detect_live_physical(char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif
