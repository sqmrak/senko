#ifndef PROFILES_H
#define PROFILES_H

#include <stddef.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* profiles exported by other clients. only the outbound kinds senko can
   actually run are converted; anything else is skipped so an imported list
   never contains a node the tunnel cannot open */

/* a clash / clash-meta yaml document with a proxies: block */
int profiles_looks_like_clash(const char *blob, size_t len);
size_t profiles_parse_clash(const char *blob, size_t len,
                            vl_server_t *out, size_t max);

/* a shadowrocket / surge ini profile with a [Proxy] section */
int profiles_looks_like_surge(const char *blob, size_t len);
size_t profiles_parse_surge(const char *blob, size_t len,
                            vl_server_t *out, size_t max);

#ifdef __cplusplus
}
#endif

#endif /* profiles_h */
