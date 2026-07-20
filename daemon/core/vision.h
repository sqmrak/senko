#ifndef VISION_H
#define VISION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VISION_CMD_CONTINUE 0
#define VISION_CMD_END      1
#define VISION_CMD_DIRECT   2

/* frame early client bytes until the tls stream reaches direct mode */
typedef struct {
    uint8_t uuid[16];
    int     uuid_sent;
    int     padding_active;
    int     packets_left;
    int     bootstrap_sent;
    int     client_hello_seen;
    int     client_tls_app_records;
    int     direct_sent;
    uint32_t prng;
} vision_wrap_t;

/* retain only partial framing bytes so direct payload is never buffered twice */
typedef struct {
    uint8_t  uuid[16];
    int      matched;
    int      remaining_command;
    int32_t  remaining_content;
    int32_t  remaining_padding;
    int      current_command;
    int      direct;
    uint8_t  stash[24];
    size_t   stash_len;
} vision_unpad_t;

void vision_wrap_init(vision_wrap_t *ctx, const uint8_t uuid[16]);

int vision_wrap_bootstrap(vision_wrap_t *ctx, uint8_t *out, size_t cap, size_t *out_len);

int vision_wrap(vision_wrap_t *ctx, const uint8_t *in, size_t in_len,
                uint8_t *out, size_t cap, size_t *out_len);

void vision_unpad_init(vision_unpad_t *ctx, const uint8_t uuid[16]);

/* remove vision framing and latch raw passthrough at direct command */
int vision_unpad(vision_unpad_t *ctx, const uint8_t *in, size_t in_len,
                 uint8_t *out, size_t cap, size_t *out_len, int *switched_direct);

#ifdef __cplusplus
}
#endif

#endif /* vision_h */
