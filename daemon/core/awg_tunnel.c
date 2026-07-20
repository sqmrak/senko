#include "awg_tunnel.h"

#include "reality_crypto.h"

#include <limits.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <string.h>

#define AWG_TRANSPORT_FIXED 32

static void write_le32(uint8_t out[4], uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static uint32_t read_le32(const uint8_t in[4]) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
           ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static void write_le64(uint8_t out[8], uint64_t value) {
    for (size_t i = 0; i < 8; ++i) out[i] = (uint8_t)(value >> (8 * i));
}

static uint64_t read_le64(const uint8_t in[8]) {
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) value |= (uint64_t)in[i] << (8 * i);
    return value;
}

static void nonce_for_counter(uint64_t counter, uint8_t nonce[12]) {
    memset(nonce, 0, 12);
    write_le64(nonce + 4, counter);
}

static uint32_t random_u32(void) {
    uint8_t bytes[4];
    if (RAND_bytes(bytes, sizeof bytes) != 1) return 0;
    return read_le32(bytes);
}

static uint32_t choose_header(uint32_t min, uint32_t max) {
    if (min == max) return min;
    uint64_t span = (uint64_t)max - min + 1;
    return min + (uint32_t)((uint64_t)random_u32() % span);
}

static size_t inner_packet_length(const uint8_t *packet, size_t packet_len) {
    if (!packet_len) return 0;
    if ((packet[0] >> 4) == 4 && packet_len >= 4) {
        size_t declared = ((size_t)packet[2] << 8) | packet[3];
        return declared >= 20 && declared <= packet_len ? declared : packet_len;
    }
    if ((packet[0] >> 4) == 6 && packet_len >= 6) {
        size_t declared = 40 + (((size_t)packet[4] << 8) | packet[5]);
        return declared <= packet_len ? declared : packet_len;
    }
    return packet_len;
}

void awg_tunnel_init(awg_tunnel_t *tunnel, const awg_config_t *cfg) {
    if (!tunnel) return;
    memset(tunnel, 0, sizeof *tunnel);
    awg_handshake_init(&tunnel->handshake, cfg);
}

awg_tun_status_t awg_tunnel_seal(awg_tunnel_t *tunnel,
                                 const uint8_t *packet, size_t packet_len,
                                 uint8_t *out, size_t cap, size_t *out_len) {
    if (!tunnel || !tunnel->handshake.cfg || (packet_len && !packet) || !out || !out_len)
        return AWG_TUN_ERR_ARG;
    const awg_config_t *cfg = tunnel->handshake.cfg;
    if (!tunnel->handshake.established || tunnel->send_counter == UINT64_MAX)
        return AWG_TUN_ERR_CRYPTO;
    size_t prefix = cfg->padding[3];
    size_t padded_len = (packet_len + 15U) & ~(size_t)15U;
    size_t need = prefix + AWG_TRANSPORT_FIXED + padded_len;
    if (need > cap || need > AWG_DATAGRAM_MAX) return AWG_TUN_ERR_SPACE;
    if (prefix && RAND_bytes(out, (int)prefix) != 1) return AWG_TUN_ERR_CRYPTO;
    uint8_t *wire = out + prefix;
    uint64_t counter = tunnel->send_counter++;
    write_le32(wire, choose_header(cfg->header_min[3], cfg->header_max[3]));
    write_le32(wire + 4, tunnel->handshake.receiver_index);
    write_le64(wire + 8, counter);
    uint8_t nonce[12];
    uint8_t tag[16];
    nonce_for_counter(counter, nonce);
    if (packet_len) memmove(wire + 16, packet, packet_len);
    if (padded_len > packet_len) memset(wire + 16 + packet_len, 0, padded_len - packet_len);
    int ok = rc_chacha20poly1305_seal(tunnel->handshake.send_key, nonce, NULL, 0,
                                      wire + 16, padded_len, wire + 16, tag) == RC_OK;
    if (ok) memcpy(wire + 16 + padded_len, tag, sizeof tag);
    OPENSSL_cleanse(nonce, sizeof nonce);
    OPENSSL_cleanse(tag, sizeof tag);
    if (!ok) return AWG_TUN_ERR_CRYPTO;
    *out_len = need;
    return AWG_TUN_OK;
}

awg_tun_status_t awg_tunnel_open(awg_tunnel_t *tunnel,
                                 const uint8_t *packet, size_t packet_len,
                                 uint8_t *out, size_t cap, size_t *out_len) {
    if (!tunnel || !tunnel->handshake.cfg || !packet || !out || !out_len)
        return AWG_TUN_ERR_ARG;
    const awg_config_t *cfg = tunnel->handshake.cfg;
    size_t prefix = cfg->padding[3];
    if (!tunnel->handshake.established || packet_len < prefix + AWG_TRANSPORT_FIXED)
        return AWG_TUN_ERR_FORMAT;
    const uint8_t *wire = packet + prefix;
    uint32_t header = read_le32(wire);
    if (header < cfg->header_min[3] || header > cfg->header_max[3] ||
        read_le32(wire + 4) != tunnel->handshake.sender_index)
        return AWG_TUN_ERR_FORMAT;
    uint64_t counter = read_le64(wire + 8);
    if (tunnel->have_recv_counter) {
        if (counter > tunnel->recv_counter) {
            uint64_t shift = counter - tunnel->recv_counter;
            tunnel->recv_window = shift >= 64 ? 1 : (tunnel->recv_window << shift) | 1U;
        } else {
            uint64_t distance = tunnel->recv_counter - counter;
            if (distance >= 64 || (tunnel->recv_window & (UINT64_C(1) << distance)))
                return AWG_TUN_ERR_REPLAY;
            tunnel->recv_window |= UINT64_C(1) << distance;
        }
    } else {
        tunnel->recv_window = 1;
    }
    size_t plain_len = packet_len - prefix - AWG_TRANSPORT_FIXED;
    if (plain_len > cap) return AWG_TUN_ERR_SPACE;
    uint8_t nonce[12];
    nonce_for_counter(counter, nonce);
    int ok = rc_chacha20poly1305_open(tunnel->handshake.recv_key, nonce, NULL, 0,
                                      wire + 16, plain_len, wire + 16 + plain_len, out) == RC_OK;
    OPENSSL_cleanse(nonce, sizeof nonce);
    if (!ok) return AWG_TUN_ERR_AUTH;
    if (!tunnel->have_recv_counter || counter > tunnel->recv_counter)
        tunnel->recv_counter = counter;
    tunnel->have_recv_counter = 1;
    *out_len = inner_packet_length(out, plain_len);
    return AWG_TUN_OK;
}
