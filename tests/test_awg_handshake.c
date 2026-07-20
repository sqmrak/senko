#include "awg_config.h"
#include "awg_handshake.h"

#include <openssl/evp.h>
#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *what) {
    if (condition) return 0;
    fprintf(stderr, "failed: %s\n", what);
    return 1;
}

int main(void) {
    static const char config_text[] =
        "[Interface]\n"
        "PrivateKey = AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "S1 = 11\nS2 = 0\nJc = 0\nJmin = 0\nJmax = 0\n"
        "H1 = 7\nH2 = 8\nH3 = 9\nH4 = 10\nMTU = 1280\n"
        "I1 = <b 0x0102><r 3><rd 4><rc 5><t>\n"
        "[Peer]\n"
        "PublicKey = CQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "Endpoint = 127.0.0.1:51820\n";
    awg_config_t cfg;
    char reason[128];
    int bad = 0;
    uint8_t vector_key[32], vector_out[32];
    static const uint8_t vector_expected[32] = {0xa2,0x81,0xf7,0x25,0x75,0x49,0x69,0xa7,0x02,0xf6,0xfe,0x36,0xfc,0x59,0x1b,0x7d,0xef,0x86,0x6e,0x4b,0x70,0x17,0x3e,0xce,0x40,0x2f,0xc0,0x1c,0x06,0x4d,0x6b,0x65};
    static const uint8_t vector_expected_16[16] = {0x61,0xba,0x5f,0x16,0x5c,0x19,0x46,0x92,0xe0,0x9d,0x12,0x52,0x0c,0xc4,0xc7,0x4a};
    static const uint8_t long_vector_expected_16[16] = {0x07,0x2e,0x94,0xff,0xbf,0xc6,0x84,0xbd,0x8a,0xb2,0xa1,0xb9,0xda,0xde,0x2f,0xd5};
    static const uint8_t mac1_key_expected[32] = {0x4f,0xb2,0x52,0x7a,0xc9,0x56,0x00,0x15,0x53,0xdc,0x1a,0xd9,0xb5,0x51,0x71,0xb7,0xd9,0xda,0xa3,0xde,0x1c,0xdb,0xcd,0x71,0x45,0x18,0x30,0x17,0x0c,0x7e,0x6c,0xac};
    for (size_t i = 0; i < sizeof vector_key; ++i) vector_key[i] = (uint8_t)i;
    uint8_t long_vector_input[116];
    for (size_t i = 0; i < sizeof long_vector_input; ++i) long_vector_input[i] = (uint8_t)i;
    bad |= expect(awg_blake2s_keyed(vector_key, sizeof vector_key, (const uint8_t *)"abc", 3, vector_out) == 0 &&
                  memcmp(vector_out, vector_expected, sizeof vector_out) == 0, "keyed blake2s vector");
    bad |= expect(awg_blake2s_keyed_16(vector_key, sizeof vector_key, (const uint8_t *)"abc", 3, vector_out) == 0 &&
                  memcmp(vector_out, vector_expected_16, sizeof vector_expected_16) == 0, "keyed blake2s-16 vector");
    bad |= expect(awg_blake2s_keyed_16(vector_key, sizeof vector_key, long_vector_input,
                                       sizeof long_vector_input, vector_out) == 0 &&
                  memcmp(vector_out, long_vector_expected_16, sizeof long_vector_expected_16) == 0,
                  "keyed blake2s-16 mac vector");
    uint8_t mac1_input[40];
    unsigned int mac1_len = 0;
    memcpy(mac1_input, "mac1----", 8);
    memcpy(mac1_input + 8, vector_key, sizeof vector_key);
    bad |= expect(EVP_Digest(mac1_input, sizeof mac1_input, vector_out, &mac1_len,
                             EVP_blake2s256(), NULL) == 1 && mac1_len == sizeof vector_out &&
                  memcmp(vector_out, mac1_key_expected, sizeof vector_out) == 0,
                  "wireguard mac1 key vector");
    bad |= expect(awg_config_parse(config_text, sizeof config_text - 1, &cfg,
                                   reason, sizeof reason) == AWG_CFG_OK, "config parse");

    uint8_t signature[64];
    size_t signature_len = 0;
    bad |= expect(awg_signature_expand(cfg.signature[0], signature, sizeof signature,
                                       &signature_len) == AWG_HS_OK, "signature expand");
    bad |= expect(signature_len == 18 && signature[0] == 1 && signature[1] == 2,
                  "signature size and bytes");

    awg_handshake_t hs;
    uint8_t packet[256];
    size_t packet_len = 0;
    awg_handshake_init(&hs, &cfg);
    bad |= expect(awg_handshake_build_initiation(&hs, packet, sizeof packet, &packet_len) == AWG_HS_OK,
                  "initiation build");
    bad |= expect(packet_len == AWG_INIT_PACKET_LEN + 11, "initiation padding");
    bad |= expect(packet[11] == 7 && packet[12] == 0 && packet[13] == 0 && packet[14] == 0,
                  "initiation header");
    bad |= expect(hs.sender_index != 0, "sender index");

    bad |= expect(packet[11 + 116] != 0 || packet[11 + 117] != 0,
                  "mac written after s1 prefix");

    static const char defaults_text[] =
        "[Interface]\nPrivateKey = AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "[Peer]\nPublicKey = CQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "Endpoint = 127.0.0.1:51820\n";
    bad |= expect(awg_config_parse(defaults_text, sizeof defaults_text - 1, &cfg,
                                   reason, sizeof reason) == AWG_CFG_OK &&
                  cfg.header_min[0] == 1 && cfg.header_min[1] == 2 &&
                  cfg.header_min[2] == 3 && cfg.header_min[3] == 4,
                  "wireguard-compatible default headers");

    if (awg_config_load_file("../../warp.conf", &cfg, reason, sizeof reason) == AWG_CFG_OK) {
        uint8_t warp_signature[AWG_DATAGRAM_MAX];
        size_t warp_signature_len = 0;
        bad |= expect(awg_signature_expand(cfg.signature[0], warp_signature, sizeof warp_signature,
                                           &warp_signature_len) == AWG_HS_OK &&
                      warp_signature_len < cfg.mtu,
                      "warp signature fits mtu");
        awg_handshake_init(&hs, &cfg);
        bad |= expect(awg_handshake_build_initiation(&hs, packet, sizeof packet, &packet_len) == AWG_HS_OK,
                      "warp initiation build");
    } else {
        fprintf(stderr, "skip: warp.conf not present (%s)\n", reason);
    }
    return bad ? 1 : 0;
}
