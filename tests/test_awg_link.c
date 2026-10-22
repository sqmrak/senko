#define _DEFAULT_SOURCE

#include "awg_link.h"
#include "core/senko_time.h"

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int failures;
static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

static awg_config_t cfg;

static void keys(awg_tunnel_t *client, awg_tunnel_t *server, uint32_t ci, uint32_t si,
                 uint8_t seed) {
    awg_tunnel_init(client, &cfg);
    awg_tunnel_init(server, &cfg);
    client->handshake.established = server->handshake.established = 1;
    client->handshake.sender_index = ci;
    client->handshake.receiver_index = si;
    server->handshake.sender_index = si;
    server->handshake.receiver_index = ci;
    for (size_t i = 0; i < 32; ++i) {
        client->handshake.send_key[i] = (uint8_t)(seed + i);
        client->handshake.recv_key[i] = (uint8_t)(seed + 64 + i);
        server->handshake.send_key[i] = client->handshake.recv_key[i];
        server->handshake.recv_key[i] = client->handshake.send_key[i];
    }
}

typedef struct {
    awg_link_t *link;
    awg_link_status_t status;
    char reason[128];
} runner_t;

static void *run(void *arg) {
    runner_t *r = arg;
    r->status = awg_link_run(r->link, r->reason, sizeof r->reason);
    return NULL;
}

/* one datagram within wait_ms, or -1 */
static ssize_t recv_wait(int fd, uint8_t *buf, size_t cap, int wait_ms) {
    struct pollfd p = { fd, POLLIN, 0 };
    if (poll(&p, 1, wait_ms) <= 0) return -1;
    return recv(fd, buf, cap, 0);
}

static const uint8_t ipv4_packet[] = {
    0x45, 0x00, 0x00, 0x14, 0, 0, 0, 0, 64, 17,
    0, 0, 10, 8, 0, 2, 1, 1, 1, 1
};

int main(void) {
    awg_config_init(&cfg);
    cfg.mtu = 1280;
    cfg.padding[3] = 7;
    cfg.header_min[3] = cfg.header_max[3] = 4;
    cfg.persistent_keepalive = 1;

    int dev[2], udp[2], wake[2];
    /* seqpacket keeps frame boundaries like utun and reports a closed peer */
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, dev) != 0 ||
        socketpair(AF_UNIX, SOCK_DGRAM, 0, udp) != 0 || pipe(wake) != 0) {
        perror("setup");
        return 1;
    }
    (void)fcntl(udp[0], F_SETFL, fcntl(udp[0], F_GETFL, 0) | O_NONBLOCK);
    utun_device_t device;
    utun_device_init(&device);
    ok("the device side is adopted", utun_device_adopt(&device, dev[0], "utun9") == UTUN_DEVICE_OK);

    awg_tunnel_t client, server, old_client, old_server;
    keys(&client, &server, 0x11111111U, 0x22222222U, 1);
    keys(&old_client, &old_server, 0x33333333U, 0x44444444U, 101);

    static awg_link_t link;
    awg_link_init(&link, &cfg, &device, udp[0], wake[0], &client.handshake, senko_now_ms());
    link.previous = old_client;
    link.have_previous = 1;
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    link.stats_lock = &lock;

    runner_t runner = { &link, AWG_LINK_STOPPED, "" };
    pthread_t thread;
    pthread_create(&thread, NULL, run, &runner);

    uint8_t frame[256], wire[512], opened[512];
    size_t opened_len = 0, wire_len = 0;

    ssize_t got = recv_wait(udp[1], wire, sizeof wire, 2000);
    ok("a started link announces its keys with a keepalive",
       got > 0 && awg_tunnel_open(&server, wire, (size_t)got, opened, sizeof opened,
                                  &opened_len) == AWG_TUN_OK && opened_len == 0);

    frame[0] = frame[1] = frame[2] = 0;
    frame[3] = UTUN_FAMILY_INET;
    memcpy(frame + 4, ipv4_packet, sizeof ipv4_packet);
    ok("a packet goes into the device", write(dev[1], frame, 4 + sizeof ipv4_packet) > 0);
    got = recv_wait(udp[1], wire, sizeof wire, 2000);
    ok("it reaches the server sealed",
       got > 0 && awg_tunnel_open(&server, wire, (size_t)got, opened, sizeof opened,
                                  &opened_len) == AWG_TUN_OK &&
       opened_len == sizeof ipv4_packet && memcmp(opened, ipv4_packet, opened_len) == 0);

    ok("the server seals a reply",
       awg_tunnel_seal(&server, ipv4_packet, sizeof ipv4_packet, wire, sizeof wire,
                       &wire_len) == AWG_TUN_OK);
    ok("the reply is sent", send(udp[1], wire, wire_len, 0) == (ssize_t)wire_len);
    got = recv_wait(dev[1], frame, sizeof frame, 2000);
    ok("it comes out of the device with an ipv4 family header",
       got == (ssize_t)(4 + sizeof ipv4_packet) && frame[3] == UTUN_FAMILY_INET &&
       memcmp(frame + 4, ipv4_packet, sizeof ipv4_packet) == 0);

    /* right after a renewal the server may still use the keys before it */
    ok("the server seals with the previous keys",
       awg_tunnel_seal(&old_server, ipv4_packet, sizeof ipv4_packet, wire, sizeof wire,
                       &wire_len) == AWG_TUN_OK);
    send(udp[1], wire, wire_len, 0);
    got = recv_wait(dev[1], frame, sizeof frame, 2000);
    ok("a packet under the previous keys is still delivered",
       got == (ssize_t)(4 + sizeof ipv4_packet));

    static const uint8_t junk[] = "not an amneziawg datagram at all, 48 bytes long";
    send(udp[1], junk, sizeof junk, 0);
    ok("junk from the network is not delivered", recv_wait(dev[1], frame, sizeof frame, 300) < 0);

    /* idle for a second: the persistent keepalive has to go out */
    int keepalive = 0;
    int64_t until = senko_now_ms() + 2500;
    while (!keepalive && senko_now_ms() < until) {
        got = recv_wait(udp[1], wire, sizeof wire, 500);
        if (got > 0 && awg_tunnel_open(&server, wire, (size_t)got, opened, sizeof opened,
                                       &opened_len) == AWG_TUN_OK && opened_len == 0)
            keepalive = 1;
    }
    ok("an idle link sends its keepalive", keepalive);

    char one = 1;
    write(wake[1], &one, 1);
    pthread_join(thread, NULL);
    ok("the owner stops it", runner.status == AWG_LINK_STOPPED);
    pthread_mutex_lock(&lock);
    ok("bytes are counted both ways",
       link.bytes_up == sizeof ipv4_packet && link.bytes_down == 2 * sizeof ipv4_packet);
    pthread_mutex_unlock(&lock);
    ok("the junk was counted as dropped", link.dropped >= 1);
    awg_link_clear(&link);

    /* keys older than three minutes are refused even before any packet */
    static awg_link_t stale;
    awg_link_init(&stale, &cfg, &device, udp[0], wake[0], &client.handshake,
                  senko_now_ms() - AWG_LINK_REJECT_AFTER_MS - 1);
    char reason[128];
    ok("a link whose server stopped answering ends",
       awg_link_run(&stale, reason, sizeof reason) == AWG_LINK_ERR_EXPIRED &&
       strstr(reason, "has not answered") != NULL);

    /* the device going away ends the link instead of spinning on it */
    int wake2[2];
    pipe(wake2);
    static awg_link_t orphan;
    awg_link_init(&orphan, &cfg, &device, udp[0], wake2[0], &client.handshake, senko_now_ms());
    close(dev[1]);
    runner_t r2 = { &orphan, AWG_LINK_STOPPED, "" };
    pthread_create(&thread, NULL, run, &r2);
    int64_t deadline = senko_now_ms() + 3000;
    struct pollfd never = { -1, 0, 0 };
    while (senko_now_ms() < deadline && r2.status == AWG_LINK_STOPPED && !r2.reason[0])
        poll(&never, 0, 50);
    if (!r2.reason[0]) write(wake2[1], &one, 1);
    pthread_join(thread, NULL);
    ok("a closed device ends the link", r2.status == AWG_LINK_ERR_DEVICE);

    utun_device_close(&device);
    if (failures) {
        fprintf(stderr, "%d awg link check(s) failed\n", failures);
        return 1;
    }
    puts("all awg link checks passed");
    return 0;
}
