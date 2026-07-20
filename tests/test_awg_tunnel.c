#include "awg_tunnel.h"

#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *what) {
    if (condition) return 0;
    fprintf(stderr, "failed: %s\n", what);
    return 1;
}

int main(void) {
    awg_config_t cfg;
    awg_config_init(&cfg);
    cfg.mtu = 1280;
    cfg.padding[3] = 7;
    cfg.header_min[3] = cfg.header_max[3] = 4;

    awg_tunnel_t client, server;
    awg_tunnel_init(&client, &cfg);
    awg_tunnel_init(&server, &cfg);
    client.handshake.established = server.handshake.established = 1;
    client.handshake.sender_index = 0x11111111U;
    client.handshake.receiver_index = 0x22222222U;
    server.handshake.sender_index = 0x22222222U;
    server.handshake.receiver_index = 0x11111111U;
    for (size_t i = 0; i < 32; ++i) {
        client.handshake.send_key[i] = (uint8_t)(i + 1);
        client.handshake.recv_key[i] = (uint8_t)(i + 33);
        server.handshake.send_key[i] = client.handshake.recv_key[i];
        server.handshake.recv_key[i] = client.handshake.send_key[i];
    }

    static const uint8_t inner[] = {
        0x45, 0x00, 0x00, 0x14, 0, 0, 0, 0, 64, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    uint8_t wire[128], wire_next[128], opened[128];
    size_t wire_len = 0, wire_next_len = 0, opened_len = 0;
    int bad = 0;
    bad |= expect(awg_tunnel_seal(&client, inner, sizeof inner, wire, sizeof wire, &wire_len) == AWG_TUN_OK,
                  "seal");
    bad |= expect(wire_len == 32 + 32 + 7, "wire length");
    bad |= expect(awg_tunnel_seal(&client, inner, sizeof inner, wire_next, sizeof wire_next,
                                  &wire_next_len) == AWG_TUN_OK, "second seal");
    bad |= expect(awg_tunnel_open(&server, wire_next, wire_next_len, opened, sizeof opened,
                                  &opened_len) == AWG_TUN_OK, "open second");
    bad |= expect(awg_tunnel_open(&server, wire, wire_len, opened, sizeof opened, &opened_len) == AWG_TUN_OK,
                  "open reordered");
    bad |= expect(opened_len == sizeof inner && memcmp(opened, inner, sizeof inner) == 0,
                  "payload roundtrip");
    bad |= expect(awg_tunnel_open(&server, wire_next, wire_next_len, opened, sizeof opened,
                                  &opened_len) == AWG_TUN_ERR_REPLAY, "replay rejection");
    bad |= expect(awg_tunnel_seal(&client, NULL, 0, wire, sizeof wire, &wire_len) == AWG_TUN_OK &&
                  wire_len == 32 + 7, "keepalive seal");
    bad |= expect(awg_tunnel_open(&server, wire, wire_len, opened, sizeof opened, &opened_len) == AWG_TUN_OK &&
                  opened_len == 0, "keepalive open");
    return bad ? 1 : 0;
}
