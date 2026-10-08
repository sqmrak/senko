#include "hy2_proto.h"
#include "blake2b256.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

size_t hy2_varint_put(uint64_t value, uint8_t *out, size_t cap) {
    if (!out) return 0;
    if (value < 0x40u) {
        if (cap < 1) return 0;
        out[0] = (uint8_t)value;
        return 1;
    }
    if (value < 0x4000u) {
        if (cap < 2) return 0;
        out[0] = (uint8_t)(0x40u | (value >> 8));
        out[1] = (uint8_t)value;
        return 2;
    }
    if (value < 0x40000000u) {
        if (cap < 4) return 0;
        out[0] = (uint8_t)(0x80u | (value >> 24));
        out[1] = (uint8_t)(value >> 16);
        out[2] = (uint8_t)(value >> 8);
        out[3] = (uint8_t)value;
        return 4;
    }
    if (value < ((uint64_t)1 << 62)) {
        if (cap < 8) return 0;
        out[0] = (uint8_t)(0xc0u | (value >> 56));
        for (size_t i = 1; i < 8; ++i)
            out[i] = (uint8_t)(value >> (8 * (7 - i)));
        return 8;
    }
    return 0;
}

size_t hy2_varint_get(const uint8_t *in, size_t len, uint64_t *value) {
    if (!in || !value || len == 0) return 0;
    size_t size = (size_t)1 << (in[0] >> 6);
    if (len < size) return 0;
    uint64_t v = in[0] & 0x3fu;
    for (size_t i = 1; i < size; ++i) v = (v << 8) | in[i];
    *value = v;
    return size;
}

hy2_status_t hy2_address_text(const vless_dest_t *dest, char *out, size_t cap) {
    if (!dest || !out || cap == 0) return HY2_ERR_ARG;
    char host[INET6_ADDRSTRLEN + 2];
    int n;
    switch (dest->atyp) {
    case VLESS_ADDR_IPV4:
        if (!inet_ntop(AF_INET, dest->host_addr, host, sizeof host)) return HY2_ERR_ARG;
        n = snprintf(out, cap, "%s:%u", host, (unsigned)dest->port);
        break;
    case VLESS_ADDR_IPV6:
        if (!inet_ntop(AF_INET6, dest->host_addr, host, sizeof host)) return HY2_ERR_ARG;
        n = snprintf(out, cap, "[%s]:%u", host, (unsigned)dest->port);
        break;
    case VLESS_ADDR_DOMAIN:
        if (dest->domain[0] == '\0') return HY2_ERR_ARG;
        n = snprintf(out, cap, "%s:%u", dest->domain, (unsigned)dest->port);
        break;
    default:
        return HY2_ERR_ARG;
    }
    return (n < 0 || (size_t)n >= cap) ? HY2_ERR_ARG : HY2_OK;
}

hy2_status_t hy2_tcp_request_build(const vless_dest_t *dest, uint8_t *out,
                                   size_t cap, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!dest || !out || !out_len) return HY2_ERR_ARG;
    char address[320];
    hy2_status_t status = hy2_address_text(dest, address, sizeof address);
    if (status != HY2_OK) return status;
    size_t address_len = strlen(address);

    size_t off = 0;
    size_t put = hy2_varint_put(HY2_FRAME_TCP_REQUEST, out + off, cap - off);
    if (put == 0) return HY2_ERR_ARG;
    off += put;
    put = hy2_varint_put(address_len, out + off, cap - off);
    if (put == 0) return HY2_ERR_ARG;
    off += put;
    if (cap - off < address_len) return HY2_ERR_ARG;
    memcpy(out + off, address, address_len);
    off += address_len;
    /* the server skips the padding, so none is sent */
    put = hy2_varint_put(0, out + off, cap - off);
    if (put == 0) return HY2_ERR_ARG;
    off += put;
    *out_len = off;
    return HY2_OK;
}

void hy2_tcp_response_init(hy2_tcp_response_t *r) {
    if (!r) return;
    memset(r, 0, sizeof *r);
    r->state = HY2_RESP_STATUS;
}

/* one byte at a time keeps every split of the header on the same path */
static hy2_status_t varint_step(hy2_tcp_response_t *r, uint8_t byte, int *complete) {
    *complete = 0;
    if (r->varint_have == 0) {
        r->varint_size = (uint8_t)(1u << (byte >> 6));
        r->varint = byte & 0x3fu;
    } else {
        r->varint = (r->varint << 8) | byte;
    }
    ++r->varint_have;
    if (r->varint_have == r->varint_size) {
        r->varint_have = 0;
        *complete = 1;
    }
    return HY2_OK;
}

hy2_status_t hy2_tcp_response_feed(hy2_tcp_response_t *r, const uint8_t *in,
                                   size_t len, size_t *consumed) {
    if (consumed) *consumed = 0;
    if (!r || !consumed || (!in && len)) return HY2_ERR_ARG;
    size_t off = 0;
    while (off < len && r->state != HY2_RESP_DONE) {
        int complete;
        switch (r->state) {
        case HY2_RESP_STATUS:
            r->status = in[off++];
            r->state = HY2_RESP_MESSAGE_LEN;
            break;
        case HY2_RESP_MESSAGE_LEN:
            varint_step(r, in[off++], &complete);
            if (!complete) break;
            if (r->varint > HY2_MESSAGE_MAX) { *consumed = off; return HY2_ERR_PROTO; }
            r->remaining = r->varint;
            r->state = r->remaining ? HY2_RESP_MESSAGE : HY2_RESP_PADDING_LEN;
            break;
        case HY2_RESP_MESSAGE: {
            size_t take = len - off;
            if (take > r->remaining) take = (size_t)r->remaining;
            memcpy(r->message + r->message_len, in + off, take);
            r->message_len += take;
            r->message[r->message_len] = '\0';
            off += take;
            r->remaining -= take;
            if (r->remaining == 0) r->state = HY2_RESP_PADDING_LEN;
            break;
        }
        case HY2_RESP_PADDING_LEN:
            varint_step(r, in[off++], &complete);
            if (!complete) break;
            if (r->varint > HY2_PADDING_MAX) { *consumed = off; return HY2_ERR_PROTO; }
            r->remaining = r->varint;
            r->state = r->remaining ? HY2_RESP_PADDING : HY2_RESP_DONE;
            break;
        case HY2_RESP_PADDING: {
            size_t take = len - off;
            if (take > r->remaining) take = (size_t)r->remaining;
            off += take;
            r->remaining -= take;
            if (r->remaining == 0) r->state = HY2_RESP_DONE;
            break;
        }
        case HY2_RESP_DONE:
            break;
        }
    }
    *consumed = off;
    if (r->state != HY2_RESP_DONE) return HY2_NEED_MORE;
    return r->status == 0 ? HY2_OK : HY2_REJECTED;
}

static hy2_status_t salamander_mask(const uint8_t *key, size_t key_len,
                                    const uint8_t *salt, const uint8_t *in,
                                    size_t len, uint8_t *out) {
    uint8_t material[HY2_SALAMANDER_KEY_MAX + HY2_SALAMANDER_SALT_LEN];
    uint8_t digest[32];
    if (!key || key_len == 0 || key_len > HY2_SALAMANDER_KEY_MAX) return HY2_ERR_ARG;
    memcpy(material, key, key_len);
    memcpy(material + key_len, salt, HY2_SALAMANDER_SALT_LEN);
    blake2b256(material, key_len + HY2_SALAMANDER_SALT_LEN, digest);
    for (size_t i = 0; i < len; ++i) out[i] = in[i] ^ digest[i % sizeof digest];
    return HY2_OK;
}

hy2_status_t hy2_salamander_wrap(const uint8_t *key, size_t key_len,
                                 const uint8_t salt[HY2_SALAMANDER_SALT_LEN],
                                 const uint8_t *in, size_t len,
                                 uint8_t *out, size_t cap, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!salt || !in || !out || !out_len || len > cap ||
        cap - len < HY2_SALAMANDER_SALT_LEN)
        return HY2_ERR_ARG;
    memcpy(out, salt, HY2_SALAMANDER_SALT_LEN);
    hy2_status_t status = salamander_mask(key, key_len, salt, in, len,
                                          out + HY2_SALAMANDER_SALT_LEN);
    if (status == HY2_OK) *out_len = len + HY2_SALAMANDER_SALT_LEN;
    return status;
}

hy2_status_t hy2_salamander_unwrap(const uint8_t *key, size_t key_len,
                                   const uint8_t *in, size_t len,
                                   uint8_t *out, size_t cap, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!in || !out || !out_len || len < HY2_SALAMANDER_SALT_LEN) return HY2_ERR_ARG;
    size_t body = len - HY2_SALAMANDER_SALT_LEN;
    if (body > cap) return HY2_ERR_ARG;
    hy2_status_t status = salamander_mask(key, key_len, in,
                                          in + HY2_SALAMANDER_SALT_LEN, body, out);
    if (status == HY2_OK) *out_len = body;
    return status;
}
