#ifndef HAPP_H
#define HAPP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* unwrap happ:// deep links into plaintext (usually vless:// or a link list) */
int happ_unwrap(const char *uri, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* happ_h */
