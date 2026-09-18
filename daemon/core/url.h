#ifndef URL_H
#define URL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int      is_https; /* select tls for https and plain tcp for http */
    char     host[256];
    uint16_t port; /* preserve the scheme default when omitted */
    char     path[1024]; /* retain the request path and query */
} url_t;

typedef enum {
    URL_OK         =  0,
    URL_ERR_ARG    = -1,
    URL_ERR_SCHEME = -2, /* reject schemes outside http and https */
    URL_ERR_HOST   = -3,
    URL_ERR_PORT   = -4,
    URL_ERR_TOOLONG= -5,
    URL_ERR_UNSAFE = -6
} url_status_t;

url_status_t url_parse(const char *url, url_t *out);

/* resolve an http redirect against the current request url */
url_status_t url_resolve_redirect(const url_t *base, const char *location,
                                  char *buf, size_t cap);

/* use close-delimited requests so old http stacks need no chunk support */
url_status_t url_build_get(const url_t *u, char *buf, size_t cap, size_t *out_len);
url_status_t url_build_get_cookie(const url_t *u, const char *cookie,
                                  char *buf, size_t cap, size_t *out_len);
/* the stable per-device id sent as x-hwid; generated and stored on first use.
   out needs at least 33 bytes */
void url_device_hwid(char *out, size_t cap);

/* issue a new id. panels that bind a subscription to a device see the next
   refresh as a different device, which is the only way to test that binding
   without a second phone */
void url_device_hwid_reset(char *out, size_t cap);

url_status_t url_build_get_cookie_header(const url_t *u, const char *cookie,
                                         const char *request_header,
                                         char *buf, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* url_h */
