#include "vision.h"

#include <string.h>
#include <sys/time.h>

#define VISION_DEFAULT_PAD_PACKETS 8
#define VISION_LONG_PAD_THRESHOLD 900
#define VISION_LONG_PAD_RANDOM 500
#define VISION_LONG_PAD_BASE 900
#define VISION_SHORT_PAD_RANDOM 256

static uint32_t seed_now(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)tv.tv_sec ^ ((uint32_t)tv.tv_usec << 11) ^ 0xa5a5c3u;
}

static uint16_t prng_mod(vision_wrap_t *ctx, uint16_t mod) {
    if (mod == 0) return 0;
    uint32_t x = ctx->prng ? ctx->prng : 0x13579bdfu;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    ctx->prng = x;
    return (uint16_t)(x % mod);
}

static uint16_t padding_len(vision_wrap_t *ctx, size_t content_len, int long_padding) {
    if (long_padding && content_len < VISION_LONG_PAD_THRESHOLD) {
        uint16_t extra = prng_mod(ctx, VISION_LONG_PAD_RANDOM);
        size_t want = (size_t)VISION_LONG_PAD_BASE + extra;
        if (want > content_len) want -= content_len;
        else want = 0;
        if (want > 0xffff) want = 0xffff;
        return (uint16_t)want;
    }
    return prng_mod(ctx, VISION_SHORT_PAD_RANDOM);
}

static void fill_padding(vision_wrap_t *ctx, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; ++i)
        out[i] = (uint8_t)prng_mod(ctx, 256);
}

static int scan_tls_records(const uint8_t *in, size_t len,
                            int *has_client_hello, int *app_records) {
    size_t pos = 0;
    int ch = 0, app = 0;
    if (has_client_hello) *has_client_hello = 0;
    if (app_records) *app_records = 0;
    if (!in || len < 5) return 0;

    while (pos < len) {
        if (len - pos < 5) return 0;
        uint8_t rtype = in[pos];
        uint8_t vmajor = in[pos + 1];
        size_t rlen = ((size_t)in[pos + 3] << 8) | in[pos + 4];
        if ((rtype != 0x14 && rtype != 0x16 && rtype != 0x17) ||
            vmajor != 0x03 || rlen > 18432 || pos + 5 + rlen > len)
            return 0;
        if (rtype == 0x16 && rlen >= 4 && in[pos + 5] == 0x01)
            ch = 1;
        else if (rtype == 0x17)
            app++;
        pos += 5 + rlen;
    }
    if (has_client_hello) *has_client_hello = ch;
    if (app_records) *app_records = app;
    return 1;
}

void vision_wrap_init(vision_wrap_t *ctx, const uint8_t uuid[16]) {
    memset(ctx, 0, sizeof *ctx);
    memcpy(ctx->uuid, uuid, 16);
    ctx->padding_active = 1;
    ctx->packets_left = VISION_DEFAULT_PAD_PACKETS;
    ctx->prng = seed_now() ^ ((uint32_t)uuid[0] << 24) ^ ((uint32_t)uuid[15] << 8);
}

static int build_block(vision_wrap_t *ctx, uint8_t cmd,
                       const uint8_t *in, size_t in_len, int long_padding,
                       uint8_t *out, size_t cap, size_t *out_len) {
    if (in_len > 0xffff) in_len = 0xffff;
    uint16_t pad = padding_len(ctx, in_len, long_padding);
    size_t prefix = ctx->uuid_sent ? 0 : 16;
    size_t total = prefix + 5 + in_len + pad;
    if (total > cap) return -1;

    size_t o = 0;
    if (!ctx->uuid_sent) { memcpy(out, ctx->uuid, 16); o = 16; ctx->uuid_sent = 1; }
    out[o++] = cmd;
    out[o++] = (uint8_t)(in_len >> 8);
    out[o++] = (uint8_t)(in_len & 0xff);
    out[o++] = (uint8_t)(pad >> 8);
    out[o++] = (uint8_t)(pad & 0xff);
    if (in_len) { memcpy(out + o, in, in_len); o += in_len; }
    if (pad) { fill_padding(ctx, out + o, pad); o += pad; }
    *out_len = o;
    return 0;
}

int vision_wrap_bootstrap(vision_wrap_t *ctx, uint8_t *out, size_t cap, size_t *out_len) {
    if (!out_len) return -1;
    *out_len = 0;
    if (!ctx || !out) return -1;
    if (!ctx->padding_active || ctx->bootstrap_sent) return 0;
    ctx->bootstrap_sent = 1;
    return build_block(ctx, VISION_CMD_CONTINUE, NULL, 0, 1, out, cap, out_len);
}

int vision_wrap(vision_wrap_t *ctx, const uint8_t *in, size_t in_len,
                uint8_t *out, size_t cap, size_t *out_len) {
    if (!out_len) return -1;
    *out_len = 0;
    if (!ctx || !out) return -1;
    if (in_len == 0) return 0;

    if (!ctx->padding_active) {
        if (in_len > cap) return -1;
        memcpy(out, in, in_len);
        *out_len = in_len;
        return 0;
    }

    if (in_len > 0xffff) in_len = 0xffff;
    int has_ch = 0, app_records = 0;
    int tls_records = scan_tls_records(in, in_len, &has_ch, &app_records);
    if (tls_records && has_ch) ctx->client_hello_seen = 1;

    uint8_t cmd = (ctx->packets_left <= 1) ? VISION_CMD_END : VISION_CMD_CONTINUE;
    if (tls_records && ctx->client_hello_seen && app_records > 0) {
        int prev_app = ctx->client_tls_app_records;
        ctx->client_tls_app_records += app_records;
        if (prev_app > 0 || app_records > 1)
            cmd = VISION_CMD_DIRECT;
    }

    int long_padding = (in_len < VISION_LONG_PAD_THRESHOLD) ? 1 : 0;
    int r = build_block(ctx, cmd, in, in_len, long_padding, out, cap, out_len);
    if (r != 0) return r;
    if (ctx->packets_left > 0) ctx->packets_left--;
    if (cmd != VISION_CMD_CONTINUE) ctx->padding_active = 0;
    if (cmd == VISION_CMD_DIRECT) ctx->direct_sent = 1;
    return 0;
}

void vision_unpad_init(vision_unpad_t *ctx, const uint8_t uuid[16]) {
    memset(ctx, 0, sizeof *ctx);
    memcpy(ctx->uuid, uuid, 16);
    ctx->remaining_command = -1;
    ctx->remaining_content = -1;
    ctx->remaining_padding = -1;
}

int vision_unpad(vision_unpad_t *ctx, const uint8_t *in, size_t in_len,
                 uint8_t *out, size_t cap, size_t *out_len, int *switched_direct) {
    *out_len = 0;
    if (switched_direct) *switched_direct = 0;

    size_t o = 0;
    size_t i = 0;

    if (ctx->direct) {
        if (in_len > cap) return -1;
        if (in_len) memcpy(out, in, in_len);
        *out_len = in_len;
        return 0;
    }

/* walk stash+input as one logical stream */
    while (i < in_len || ctx->stash_len > 0) {
        if (ctx->remaining_command == -1) {
            while (ctx->stash_len < 16 && i < in_len) ctx->stash[ctx->stash_len++] = in[i++];
            if (ctx->stash_len < 16) break; /* need more */
            if (memcmp(ctx->stash, ctx->uuid, 16) != 0) {
                if (ctx->stash_len + (in_len - i) > cap - o) return -1;
                memcpy(out + o, ctx->stash, ctx->stash_len); o += ctx->stash_len;
                memcpy(out + o, in + i, in_len - i); o += in_len - i;
                ctx->stash_len = 0; ctx->direct = 1;
                if (switched_direct) *switched_direct = 1;
                *out_len = o;
                return 0;
            }
            ctx->stash_len = 0;
            ctx->remaining_command = 5;
            ctx->remaining_content = 0;
            ctx->remaining_padding = 0;
        }

        while (ctx->remaining_command > 0) {
            if (i >= in_len) { *out_len = o; return 0; } /* header spans reads */
            uint8_t b = in[i++];
            switch (ctx->remaining_command) {
                case 5: ctx->current_command = b; break;
                case 4: ctx->remaining_content = ((int32_t)b) << 8; break;
                case 3: ctx->remaining_content |= b; break;
                case 2: ctx->remaining_padding = ((int32_t)b) << 8; break;
                case 1: ctx->remaining_padding |= b; break;
            }
            ctx->remaining_command--;
        }

        if (ctx->remaining_content > 0) {
            size_t take = in_len - i;
            if (take > (size_t)ctx->remaining_content) take = (size_t)ctx->remaining_content;
            if (take > cap - o) return -1;
            memcpy(out + o, in + i, take); o += take;
            i += take;
            ctx->remaining_content -= (int32_t)take;
            if (ctx->remaining_content > 0) { *out_len = o; return 0; }
        }

        if (ctx->remaining_content == 0 && ctx->remaining_padding > 0) {
            size_t skip = in_len - i;
            if (skip > (size_t)ctx->remaining_padding) skip = (size_t)ctx->remaining_padding;
            i += skip;
            ctx->remaining_padding -= (int32_t)skip;
            if (ctx->remaining_padding > 0) { *out_len = o; return 0; }
        }

        if (ctx->remaining_command <= 0 && ctx->remaining_content <= 0 &&
            ctx->remaining_padding <= 0) {
            if (ctx->current_command == VISION_CMD_CONTINUE) {
                ctx->remaining_command = 5; /* next block, no uuid */
                ctx->remaining_content = 0;
                ctx->remaining_padding = 0;
            } else {
                if (ctx->current_command == VISION_CMD_DIRECT) {
                    ctx->direct = 1;
                    if (switched_direct) *switched_direct = 1;
                    size_t rest = in_len - i;
                    if (rest > cap - o) return -1;
                    memcpy(out + o, in + i, rest); o += rest; i = in_len;
                }
                ctx->remaining_command = -1; /* end switches back to idle */
                ctx->remaining_content = -1;
                ctx->remaining_padding = -1;
            }
        }
    }

    *out_len = o;
    return 0;
}
