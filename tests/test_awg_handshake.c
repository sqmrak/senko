#include "awg_config.h"
#include "awg_handshake.h"

#include <openssl/evp.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "awg_response_fixture.inc"

static int expect(int condition, const char *what) {
    if (condition) return 0;
    fprintf(stderr, "failed: %s\n", what);
    return 1;
}

static void response_state(awg_handshake_t *hs, awg_config_t *cfg) {
    awg_config_init(cfg);
    memcpy(cfg->private_key, response_private_key, AWG_KEY_LEN);
    memcpy(cfg->peer_public_key, response_peer_public_key, AWG_KEY_LEN);
    awg_handshake_init(hs, cfg);
    memcpy(hs->ephemeral_private, response_ephemeral_private, AWG_KEY_LEN);
    memcpy(hs->chain_key, response_chain_key, AWG_KEY_LEN);
    memcpy(hs->hash, response_hash, AWG_KEY_LEN);
    hs->sender_index = 0x55667788U;
}

static int test_response(void) {
    awg_config_t cfg;
    awg_handshake_t hs;
    int bad = 0;
    response_state(&hs, &cfg);
    bad |= expect(awg_handshake_consume_response(&hs, response_packet,
                    sizeof response_packet) == AWG_HS_OK && hs.established &&
                  hs.receiver_index == 0x11223344U &&
                  memcmp(hs.send_key, response_send_key, AWG_KEY_LEN) == 0 &&
                  memcmp(hs.recv_key, response_recv_key, AWG_KEY_LEN) == 0,
                  "independent wireguard response and session keys");

    for (size_t n = 0; n < sizeof response_packet; ++n) {
        response_state(&hs, &cfg);
        bad |= expect(awg_handshake_consume_response(&hs, response_packet, n) ==
                      AWG_HS_ERR_FORMAT && !hs.established, "truncated response refused");
    }
    response_state(&hs, &cfg);
    ++hs.sender_index;
    bad |= expect(awg_handshake_consume_response(&hs, response_packet,
                    sizeof response_packet) == AWG_HS_ERR_FORMAT && !hs.established,
                  "response for another initiation refused");

    uint8_t damaged[sizeof response_packet];
    memcpy(damaged, response_packet, sizeof damaged);
    damaged[44] ^= 1;
    response_state(&hs, &cfg);
    bad |= expect(awg_handshake_consume_response(&hs, damaged, sizeof damaged) ==
                  AWG_HS_ERR_AUTH && !hs.established, "damaged response tag refused");
    response_state(&hs, &cfg);
    cfg.has_preshared_key = 1;
    memset(cfg.preshared_key, 1, AWG_KEY_LEN);
    bad |= expect(awg_handshake_consume_response(&hs, response_packet,
                    sizeof response_packet) == AWG_HS_ERR_AUTH && !hs.established,
                  "response with another preshared key refused");
    return bad;
}

static int test_warp_profiles(void) {
    static const struct { size_t i1_len; size_t i2_len; } profiles[] = {
        {1249, 0}, {347, 244}
    };
    int bad = 0;
    for (size_t i = 0; i < sizeof profiles / sizeof profiles[0]; ++i) {
        char i1[2506], i2[496], text[4096], reason[160];
        memcpy(i1, "<b 0x", 5);
        memset(i1 + 5, 'a', profiles[i].i1_len * 2);
        strcpy(i1 + 5 + profiles[i].i1_len * 2, ">");
        i2[0] = '\0';
        if (profiles[i].i2_len) {
            memcpy(i2, "<b 0x", 5);
            memset(i2 + 5, 'b', profiles[i].i2_len * 2);
            strcpy(i2 + 5 + profiles[i].i2_len * 2, ">");
        }
        int len = snprintf(text, sizeof text,
            "[Interface]\nPrivateKey = AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
            "Address = 172.16.0.2, 2001:db8::2\n"
            "DNS = 1.1.1.1, 1.0.0.1, 2606:4700:4700::1111, 2606:4700:4700::1001\t\n"
            "MTU = 1280\nS1 = 0\nS2 = 0\nS3 = 0\nS4 = 0\n"
            "Jc = 4\nJmin = 40\nJmax = 70\nH1 = 1\nH2 = 2\nH3 = 3\nH4 = 4\n"
            "I1 = %s\nI2 = %s\n[Peer]\n"
            "PublicKey = CQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
            "AllowedIPs = 0.0.0.0/0, ::/0\nEndpoint = 127.0.0.1:51820\n", i1, i2);
        awg_config_t cfg;
        if (len < 0 || (size_t)len >= sizeof text ||
            awg_config_parse(text, (size_t)len, &cfg, reason, sizeof reason) != AWG_CFG_OK) {
            bad |= expect(0, "warp exporter profile parses");
            continue;
        }
        bad |= expect(cfg.address_count == 2 && cfg.dns_count == 4 &&
                      cfg.padding[0] == 0 && cfg.jc == 4,
                      "warp network and wire fields preserved");
        int fd[2];
        if (socketpair(AF_UNIX, SOCK_DGRAM, 0, fd) != 0) {
            perror("warp socketpair");
            return 1;
        }
        awg_handshake_t hs;
        bad |= expect(awg_handshake_establish_fd(fd[0], -1, &cfg, 50, &hs,
                         reason, sizeof reason) == AWG_HS_ERR_TIMEOUT &&
                      strcmp(reason, "handshake timed out without udp response") == 0,
                      "silent endpoint reports no udp response");
        uint8_t wire[2048];
        size_t count = 0;
        struct pollfd pfd = {fd[1], POLLIN, 0};
        while (poll(&pfd, 1, 0) > 0 && count < 8) {
            ssize_t got = recv(fd[1], wire, sizeof wire, 0);
            size_t signature_count = profiles[i].i2_len ? 2 : 1;
            if (count < signature_count) {
                size_t want = count == 0 ? profiles[i].i1_len : profiles[i].i2_len;
                uint8_t byte = count == 0 ? 0xaa : 0xbb;
                bad |= expect(got == (ssize_t)want, "warp signature length on the wire");
                if (got == (ssize_t)want)
                    for (size_t j = 0; j < want; ++j)
                        bad |= expect(wire[j] == byte, "warp signature bytes on the wire");
            } else if (count < signature_count + cfg.jc) {
                bad |= expect(got >= 40 && got <= 70, "warp junk follows signatures");
            } else {
                bad |= expect(got == AWG_INIT_PACKET_LEN && wire[0] == 1 &&
                              wire[1] == 0 && wire[2] == 0 && wire[3] == 0,
                              "wireguard initiation follows warp junk");
            }
            ++count;
        }
        bad |= expect(count == (profiles[i].i2_len ? 7U : 6U),
                      "complete warp handshake train sent");
        close(fd[0]);
        close(fd[1]);
    }
    return bad;
}

int main(void) {
    static const char config_text[] =
        "[Interface]\n"
        "PrivateKey = AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "Address = 10.0.0.2/32\n"
        "S1 = 11\nS2 = 0\nJc = 0\nJmin = 0\nJmax = 0\n"
        "H1 = 7\nH2 = 8\nH3 = 9\nH4 = 10\nMTU = 1280\n"
        "I1 = <b 0x0102><r 3><rd 4><rc 5><t>\n"
        "[Peer]\n"
        "PublicKey = CQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "Endpoint = 127.0.0.1:51820\n";
    awg_config_t cfg;
    char reason[128];
    int bad = 0;
    bad |= test_response();
    bad |= test_warp_profiles();
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
    uint8_t packet[512];
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
    cfg.random_trailers = 1;
    awg_handshake_init(&hs, &cfg);
    bad |= expect(awg_handshake_build_initiation(&hs, packet, sizeof packet,
                                                  &packet_len) == AWG_HS_OK &&
                  packet_len >= AWG_INIT_PACKET_LEN + 11 &&
                  packet_len <= sizeof packet && packet[11] == 7,
                  "random handshake trailer follows the authenticated message");

    static const char defaults_text[] =
        "[Interface]\nPrivateKey = AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "Address = 10.0.0.2/32\n"
        "[Peer]\nPublicKey = CQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "Endpoint = 127.0.0.1:51820\n";
    bad |= expect(awg_config_parse(defaults_text, sizeof defaults_text - 1, &cfg,
                                   reason, sizeof reason) == AWG_CFG_OK &&
                  cfg.header_min[0] == 1 && cfg.header_min[1] == 2 &&
                  cfg.header_min[2] == 3 && cfg.header_min[3] == 4,
                  "wireguard-compatible default headers");

    return bad ? 1 : 0;
}
