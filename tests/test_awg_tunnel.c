#include "awg_tunnel.h"

#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *what) {
    if (condition) return 0;
    fprintf(stderr, "failed: %s\n", what);
    return 1;
}


/* a fresh pair with no padding, so every datagram is the same size */
static void replay_pair(awg_config_t *cfg, awg_tunnel_t *client, awg_tunnel_t *server) {
    awg_config_init(cfg);
    cfg->mtu = 1280;
    cfg->header_min[3] = cfg->header_max[3] = 4;
    awg_tunnel_init(client, cfg);
    awg_tunnel_init(server, cfg);
    client->handshake.established = server->handshake.established = 1;
    client->handshake.sender_index = 0x11111111U;
    client->handshake.receiver_index = 0x22222222U;
    server->handshake.sender_index = 0x22222222U;
    server->handshake.receiver_index = 0x11111111U;
    for (size_t i = 0; i < 32; ++i) {
        client->handshake.send_key[i] = (uint8_t)(i + 1);
        client->handshake.recv_key[i] = (uint8_t)(i + 33);
        server->handshake.send_key[i] = client->handshake.recv_key[i];
        server->handshake.recv_key[i] = client->handshake.send_key[i];
    }
}

#define REPLAY_BURST 9000
static uint8_t replay_wire[REPLAY_BURST][96];
static size_t replay_wire_len[REPLAY_BURST];

/* the window is the one the kernel keeps, not a handful of packets */
static int check_replay_window(void) {
    static const uint8_t inner[] = {
        0x45, 0x00, 0x00, 0x14, 0, 0, 0, 0, 64, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    awg_config_t cfg;
    awg_tunnel_t client, server;
    uint8_t opened[64];
    size_t opened_len = 0;
    int bad = 0;
    replay_pair(&cfg, &client, &server);
    for (size_t i = 0; i < REPLAY_BURST; ++i)
        bad |= expect(awg_tunnel_seal(&client, inner, sizeof inner, replay_wire[i],
                                      sizeof replay_wire[i], &replay_wire_len[i]) == AWG_TUN_OK,
                      "seal a burst");
    /* the server's cores deliver the last packet of a burst first */
    bad |= expect(awg_tunnel_open(&server, replay_wire[4000], replay_wire_len[4000], opened,
                                  sizeof opened, &opened_len) == AWG_TUN_OK, "open far ahead");
    bad |= expect(awg_tunnel_open(&server, replay_wire[0], replay_wire_len[0], opened,
                                  sizeof opened, &opened_len) == AWG_TUN_OK && opened_len == sizeof inner,
                  "a packet 4000 behind the newest is still accepted");
    bad |= expect(awg_tunnel_open(&server, replay_wire[0], replay_wire_len[0], opened,
                                  sizeof opened, &opened_len) == AWG_TUN_ERR_REPLAY,
                  "that packet is not accepted twice");
    bad |= expect(awg_tunnel_open(&server, replay_wire[4000], replay_wire_len[4000], opened,
                                  sizeof opened, &opened_len) == AWG_TUN_ERR_REPLAY,
                  "the newest packet is not accepted twice");
    bad |= expect(awg_tunnel_open(&server, replay_wire[8191], replay_wire_len[8191], opened,
                                  sizeof opened, &opened_len) == AWG_TUN_OK, "move the window on");
    bad |= expect(awg_tunnel_open(&server, replay_wire[1], replay_wire_len[1], opened,
                                  sizeof opened, &opened_len) == AWG_TUN_OK,
                  "the oldest counter still inside the window is accepted");
    bad |= expect(awg_tunnel_open(&server, replay_wire[0], replay_wire_len[0], opened,
                                  sizeof opened, &opened_len) == AWG_TUN_ERR_REPLAY,
                  "a seen counter stays rejected after the window moves");
    bad |= expect(awg_tunnel_open(&server, replay_wire[8192], replay_wire_len[8192], opened,
                                  sizeof opened, &opened_len) == AWG_TUN_OK, "slide by one");
    bad |= expect(awg_tunnel_open(&server, replay_wire[1], replay_wire_len[1], opened,
                                  sizeof opened, &opened_len) == AWG_TUN_ERR_REPLAY,
                  "a counter 8191 behind is out of the window");
    bad |= expect(awg_tunnel_open(&server, replay_wire[2], replay_wire_len[2], opened,
                                  sizeof opened, &opened_len) == AWG_TUN_OK,
                  "a counter just inside the window is accepted");
    bad |= expect(awg_tunnel_open(&server, replay_wire[4001], replay_wire_len[4001], opened,
                                  sizeof opened, &opened_len) == AWG_TUN_OK,
                  "a slot the ring reused is empty for its new counter");
    /* a jump over the whole window forgets everything before it */
    bad |= expect(awg_tunnel_open(&server, replay_wire[REPLAY_BURST - 1],
                                  replay_wire_len[REPLAY_BURST - 1], opened, sizeof opened,
                                  &opened_len) == AWG_TUN_OK, "jump past the window");
    bad |= expect(awg_tunnel_open(&server, replay_wire[8192], replay_wire_len[8192], opened,
                                  sizeof opened, &opened_len) == AWG_TUN_ERR_REPLAY,
                  "a counter before a full jump is out of the window");
    /* a forged counter far ahead must not move the window */
    uint8_t forged[96];
    memcpy(forged, replay_wire[REPLAY_BURST - 2], replay_wire_len[REPLAY_BURST - 2]);
    forged[8] ^= 0x40;
    bad |= expect(awg_tunnel_open(&server, forged, replay_wire_len[REPLAY_BURST - 2], opened,
                                  sizeof opened, &opened_len) == AWG_TUN_ERR_AUTH,
                  "a forged counter fails authentication");
    bad |= expect(awg_tunnel_open(&server, replay_wire[REPLAY_BURST - 2],
                                  replay_wire_len[REPLAY_BURST - 2], opened, sizeof opened,
                                  &opened_len) == AWG_TUN_OK,
                  "the window did not move for the forgery");
    return bad;
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
    uint8_t forged[128];
    memcpy(forged, wire_next, wire_next_len);
    forged[wire_next_len - 1] ^= 1;
    bad |= expect(awg_tunnel_open(&server, forged, wire_next_len, opened, sizeof opened,
                                  &opened_len) == AWG_TUN_ERR_AUTH,
                  "forged counter fails authentication");
    bad |= expect(awg_tunnel_open(&server, wire_next, wire_next_len, opened, sizeof opened,
                                  &opened_len) == AWG_TUN_OK,
                  "forged counter does not consume replay window");
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
    cfg.content_padding.min = cfg.content_padding.max = 9;
    bad |= expect(awg_tunnel_seal(&client, inner, sizeof inner, wire, sizeof wire,
                                  &wire_len) == AWG_TUN_OK &&
                  wire_len == 7 + 32 + sizeof inner + 9,
                  "content padding uses configured addition");
    bad |= expect(awg_tunnel_open(&server, wire, wire_len, opened, sizeof opened,
                                  &opened_len) == AWG_TUN_OK &&
                  opened_len == sizeof inner && memcmp(opened, inner, sizeof inner) == 0,
                  "content padded packet roundtrips");
    cfg.content_padding.min = cfg.content_padding.max = 0;
    cfg.random_trailers = 1;
    bad |= expect(awg_tunnel_seal(&client, inner, sizeof inner, wire, sizeof wire,
                                  &wire_len) == AWG_TUN_OK && wire_len <= sizeof wire,
                  "random trailer fits caller capacity");
    bad |= expect(awg_tunnel_open(&server, wire, wire_len, opened, sizeof opened,
                                  &opened_len) == AWG_TUN_OK && opened_len == sizeof inner,
                  "random trailer packet roundtrips");
    bad |= expect(awg_tunnel_seal(&client, NULL, 0, wire, sizeof wire,
                                  &wire_len) == AWG_TUN_OK &&
                  awg_tunnel_open(&server, wire, wire_len, opened, sizeof opened,
                                  &opened_len) == AWG_TUN_OK && opened_len == 0,
                  "random padded keepalive stays a keepalive");
    cfg.random_trailers = 0;
    cfg.content_padding.min = cfg.content_padding.max = 1000;
    uint8_t large_wire[1500], large_open[1500];
    bad |= expect(awg_tunnel_seal(&client, inner, sizeof inner, large_wire,
                                  sizeof large_wire, &wire_len) == AWG_TUN_OK &&
                  wire_len == 500,
                  "content padding stays inside the learned udp window");
    bad |= expect(awg_tunnel_open(&server, large_wire, wire_len, large_open,
                                  sizeof large_open, &opened_len) == AWG_TUN_OK &&
                  opened_len == sizeof inner,
                  "large content padding preserves the inner packet");
    bad |= check_replay_window();
    return bad ? 1 : 0;
}
