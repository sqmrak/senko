#ifndef AWG_UTUN_H
#define AWG_UTUN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* open utun directly because old ios has no network extension runtime */
int awg_utun_open(char *ifname, size_t ifname_cap);

#ifdef __cplusplus
}
#endif

#endif
