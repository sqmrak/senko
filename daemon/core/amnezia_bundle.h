#ifndef AMNEZIA_BUNDLE_H
#define AMNEZIA_BUNDLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the amnezia client shares a profile as vpn://<base64url(qCompress(json))>,
   as a .vpn file holding the same base64 body, or as the plain json. all three
   wrap the amneziawg .conf inside containers[].awg.last_config.config */
#define AMZ_BUNDLE_MAX_JSON (512 * 1024)

typedef enum {
    AMZ_OK = 0,
    AMZ_ERR_ARG = -1,
    /* the input is not an amnezia bundle, so the caller can keep sniffing */
    AMZ_ERR_NOT_BUNDLE = -2,
    AMZ_ERR_DECODE = -3,
    AMZ_ERR_JSON = -4,
    /* a real bundle carrying only containers senko cannot dial */
    AMZ_ERR_UNSUPPORTED = -5,
    AMZ_ERR_SPACE = -6
} amz_status_t;

/* cheap classification for an import sniffer: no allocation, no decoding */
int amz_bundle_looks_like(const char *text, size_t len);

/* write the embedded amneziawg config text, newline terminated */
amz_status_t amz_bundle_extract_conf(const char *in, size_t len,
                                     char *out, size_t cap, size_t *out_len,
                                     char *reason, size_t reason_cap);

#ifdef __cplusplus
}
#endif

#endif
