#ifndef HAPP_H
#define HAPP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* unwrap happ:// deep links into plaintext (usually vless:// or a link list) */
int happ_unwrap(const char *uri, char *out, size_t out_cap);

/* the crypt5 body, without the happ://crypt5/ prefix. rsa wrapped chacha key,
   its own byte shuffles, and a keytable of its own, so it lives apart */
int happ_crypt5_unwrap(const char *payload, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* happ_h */
