#define _DEFAULT_SOURCE

#include "awg_split.h"
#include "core/senko_time.h"
#include "packet_fixture.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* the whole split tunnel on the host: a socketpair for the utun device, one for
   the server, real loopback sockets for the sites the rules send direct */

static int failures;
static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

static awg_config_t cfg;
static const uint8_t APP[4] = { 10, 8, 0, 2 };
static const uint8_t LOOPBACK[4] = { 127, 0, 0, 1 };
static const uint8_t ABROAD[4] = { 203, 0, 113, 5 };

static void keys(awg_tunnel_t *client, awg_tunnel_t *server) {
    awg_tunnel_init(client, &cfg);
    awg_tunnel_init(server, &cfg);
    client->handshake.established = server->handshake.established = 1;
    client->handshake.sender_index = 0x11111111U;
    client->handshake.receiver_index = 0x22222222U;
    server->handshake.sender_index = 0x22222222U;
    server->handshake.receiver_index = 0x11111111U;
    for (size_t i = 0; i < 32; ++i) {
        client->handshake.send_key[i] = (uint8_t)(1 + i);
        client->handshake.recv_key[i] = (uint8_t)(65 + i);
        server->handshake.send_key[i] = client->handshake.recv_key[i];
        server->handshake.recv_key[i] = client->handshake.send_key[i];
    }
}

typedef struct {
    awg_split_t *split;
    awg_link_status_t status;
    char reason[192];
} runner_t;

static void *run(void *arg) {
    runner_t *r = arg;
    r->status = awg_split_run(r->split, r->reason, sizeof r->reason);
    return NULL;
}

static ssize_t recv_wait(int fd, uint8_t *buf, size_t cap, int wait_ms) {
    struct pollfd p = { fd, POLLIN, 0 };
    if (poll(&p, 1, wait_ms) <= 0) return -1;
    return recv(fd, buf, cap, 0);
}

/* the next frame from the device that matches, within wait_ms; others are skipped */
typedef int (*match_fn)(const uint8_t *packet, size_t len, void *ctx);
static ssize_t frame_matching(int fd, uint8_t *frame, size_t cap, int wait_ms, match_fn match,
                              void *ctx) {
    int64_t until = senko_now_ms() + wait_ms;
    while (senko_now_ms() < until) {
        ssize_t got = recv_wait(fd, frame, cap, 100);
        if (got > 4 && match(frame + 4, (size_t)got - 4, ctx)) return got;
    }
    return -1;
}

static size_t send_frame(int fd, const uint8_t *packet, size_t len) {
    uint8_t frame[2048];
    size_t n = pkt_frame(frame, packet, len);
    return write(fd, frame, n) == (ssize_t)n ? n : 0;
}

typedef struct { uint8_t proto; uint16_t sport; uint8_t flags; size_t data; } want_t;

static int is_from_site(const uint8_t *p, size_t len, void *ctx) {
    const want_t *w = ctx;
    if (!pkt_is_v4(p) || pkt_proto(p, len) != w->proto) return 0;
    size_t l4 = pkt_l4(p, len);
    if (pkt_rd16(p + l4) != w->sport) return 0;
    if (w->proto == 6 && (p[l4 + 13] & w->flags) != w->flags) return 0;
    size_t header = w->proto == 6 ? (size_t)(p[l4 + 12] >> 4) * 4 : 8;
    return len - l4 - header >= w->data;
}

/* bound to one loopback address, on the given port or any free one when 0 */
static int listener(int type, uint32_t address, uint16_t *port) {
    int fd = socket(AF_INET, type, 0);
    struct sockaddr_in a;
    socklen_t n = sizeof a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(address);
    a.sin_port = htons(*port);
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) != 0 ||
        (type == SOCK_STREAM && listen(fd, 4) != 0) ||
        getsockname(fd, (struct sockaddr *)&a, &n) != 0) {
        perror("listener");
        return -1;
    }
    *port = ntohs(a.sin_port);
    return fd;
}

/* udp2 answers from 127.0.0.2, where a connected relay socket expects the reply */
typedef struct { int tcp, udp, udp2; } sites_t;

static void *echo_sites(void *arg) {
    sites_t *s = arg;
    int conn = -1;
    uint8_t buf[2048];
    for (;;) {
        struct pollfd p[4] = { { s->tcp, POLLIN, 0 }, { s->udp, POLLIN, 0 },
                               { s->udp2, POLLIN, 0 }, { conn, POLLIN, 0 } };
        if (poll(p, conn >= 0 ? 4 : 3, 200) < 0) return NULL;
        if (p[0].revents & POLLIN) conn = accept(s->tcp, NULL, NULL);
        for (int u = 1; u <= 2; ++u) {
            if (!(p[u].revents & POLLIN)) continue;
            struct sockaddr_in from;
            socklen_t n = sizeof from;
            ssize_t got = recvfrom(p[u].fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &n);
            if (got > 0) sendto(p[u].fd, buf, (size_t)got, 0, (struct sockaddr *)&from, n);
            if (got == 4 && memcmp(buf, "stop", 4) == 0) goto done;
        }
        if (conn >= 0 && (p[3].revents & POLLIN)) {
            ssize_t got = read(conn, buf, sizeof buf);
            if (got <= 0) { close(conn); conn = -1; }
            else if (write(conn, buf, (size_t)got) != got) break;
        }
    }
done:
    if (conn >= 0) close(conn);
    return NULL;
}

int main(void) {
    awg_config_init(&cfg);
    cfg.mtu = 1280;
    cfg.persistent_keepalive = 1;

    int dev[2], udp[2], wake[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, dev) != 0 ||
        socketpair(AF_UNIX, SOCK_DGRAM, 0, udp) != 0 || pipe(wake) != 0) {
        perror("setup");
        return 1;
    }
    int big = 1 << 20;
    setsockopt(dev[0], SOL_SOCKET, SO_SNDBUF, &big, sizeof big);
    setsockopt(dev[1], SOL_SOCKET, SO_RCVBUF, &big, sizeof big);
    (void)fcntl(udp[0], F_SETFL, fcntl(udp[0], F_GETFL, 0) | O_NONBLOCK);
    utun_device_t device;
    utun_device_init(&device);
    ok("the device side is adopted", utun_device_adopt(&device, dev[0], "utun9") == UTUN_DEVICE_OK);

    awg_tunnel_t client, server;
    keys(&client, &server);
    static awg_link_t link;
    awg_link_init(&link, &cfg, &device, udp[0], wake[0], &client.handshake, senko_now_ms());

    static ruleset_t rules;
    ruleset_init(&rules);
    static const char rule_text[] = "direct ip-cidr 127.0.0.1/32";
    static const char domain_text[] = "direct domain-suffix site.test";
    ok("the rule parses", ruleset_add_text(&rules, rule_text, strlen(rule_text), NULL) == RULES_OK &&
       ruleset_add_text(&rules, domain_text, strlen(domain_text), NULL) == RULES_OK);
    ok("rules that send traffic out ask for sorting", awg_split_wanted(&rules));
    ruleset_t quiet;
    ruleset_init(&quiet);
    ok("an empty ruleset does not", !awg_split_wanted(&quiet));
    quiet.fallback = RULE_ACTION_DIRECT;
    ok("a direct fallback does", awg_split_wanted(&quiet));

    awg_split_config_t sc;
    memset(&sc, 0, sizeof sc);
    sc.link = &link;
    sc.device = &device;
    sc.rules = &rules;
    sc.direct.ifindex = 1;
    snprintf(sc.direct.ifname, sizeof sc.direct.ifname, "lo");
    sc.wake_fd = wake[0];
    sc.mtu = 1280;
    memcpy(sc.dns, "\x0a\x08\x00\x01", 4);
    char reason[192];
    awg_split_t *split = awg_split_open(&sc, reason, sizeof reason);
    ok("the split tunnel opens", split != NULL);
    if (!split) {
        fprintf(stderr, "%s\n", reason);
        return 1;
    }

    sites_t sites;
    uint16_t tcp_port, udp_port;
    tcp_port = udp_port = 0;
    sites.tcp = listener(SOCK_STREAM, INADDR_LOOPBACK, &tcp_port);
    sites.udp = listener(SOCK_DGRAM, INADDR_LOOPBACK, &udp_port);
    sites.udp2 = listener(SOCK_DGRAM, INADDR_LOOPBACK + 1, &udp_port);
    pthread_t site_thread, loop_thread;
    pthread_create(&site_thread, NULL, echo_sites, &sites);
    runner_t runner = { split, AWG_LINK_STOPPED, "" };
    pthread_create(&loop_thread, NULL, run, &runner);

    uint8_t packet[1500], frame[2048], wire[2048], opened[2048];
    size_t opened_len = 0;
    ssize_t got;

    /* a flow the rules leave to the server is sealed unchanged */
    size_t len = pkt_udp4(packet, APP, 5000, ABROAD, 443, (const uint8_t *)"quic", 4);
    ok("a datagram abroad is written", send_frame(dev[1], packet, len) > 0);
    got = recv_wait(udp[1], wire, sizeof wire, 2000);
    ok("it reaches the server sealed and unchanged",
       got > 0 && awg_tunnel_open(&server, wire, (size_t)got, opened, sizeof opened,
                                  &opened_len) == AWG_TUN_OK &&
       opened_len == len && memcmp(opened, packet, len) == 0);

    /* a flow the rules send direct never touches the server */
    len = pkt_udp4(packet, APP, 5001, LOOPBACK, udp_port, (const uint8_t *)"direct", 6);
    ok("a direct datagram is written", send_frame(dev[1], packet, len) > 0);
    want_t from_udp = { 17, udp_port, 0, 6 };
    got = frame_matching(dev[1], frame, sizeof frame, 3000, is_from_site, &from_udp);
    ok("the site's echo comes back through the device",
       got > 0 && memcmp(frame + 4 + 20 + 8, "direct", 6) == 0 &&
       memcmp(pkt_dst(frame + 4), APP, 4) == 0);
    int leaked = 0;
    while ((got = recv_wait(udp[1], wire, sizeof wire, 200)) > 0)
        if (awg_tunnel_open(&server, wire, (size_t)got, opened, sizeof opened, &opened_len) ==
                AWG_TUN_OK && opened_len > 0 && memcmp(opened, packet, len) == 0)
            leaked = 1;
    ok("the direct datagram never went to the server", !leaked);

    /* a direct tcp connection: the stack answers the app, the site sees a real socket */
    len = pkt_tcp4(packet, APP, 40001, LOOPBACK, tcp_port, PKT_SYN, 1000, 0, 0);
    ok("a direct syn is written", send_frame(dev[1], packet, len) > 0);
    want_t synack = { 6, tcp_port, PKT_SYN | PKT_ACK, 0 };
    got = frame_matching(dev[1], frame, sizeof frame, 3000, is_from_site, &synack);
    ok("the local stack answers with a syn-ack", got > 0);
    if (got > 0) {
        const uint8_t *sp = frame + 4;
        size_t l4 = pkt_l4(sp, (size_t)got - 4);
        uint32_t isn = pkt_rd32(sp + l4 + 4);
        len = pkt_tcp4(packet, APP, 40001, LOOPBACK, tcp_port, PKT_ACK, 1001, isn + 1, 0);
        send_frame(dev[1], packet, len);
        len = pkt_tcp4(packet, APP, 40001, LOOPBACK, tcp_port, PKT_ACK, 1001, isn + 1, 100);
        send_frame(dev[1], packet, len);
        want_t echoed = { 6, tcp_port, PKT_ACK, 100 };
        got = frame_matching(dev[1], frame, sizeof frame, 3000, is_from_site, &echoed);
        ok("the site's echo of the app's bytes comes back", got > 0);
    }

    /* a tcp flow abroad is the server's */
    len = pkt_tcp4(packet, APP, 40002, ABROAD, 443, PKT_SYN, 5, 0, 0);
    ok("a syn abroad is written", send_frame(dev[1], packet, len) > 0);
    int syn_seen = 0;
    int64_t until = senko_now_ms() + 2000;
    while (!syn_seen && senko_now_ms() < until) {
        got = recv_wait(udp[1], wire, sizeof wire, 200);
        if (got > 0 && awg_tunnel_open(&server, wire, (size_t)got, opened, sizeof opened,
                                       &opened_len) == AWG_TUN_OK && opened_len == len &&
            memcmp(opened, packet, len) == 0)
            syn_seen = 1;
    }
    ok("it reaches the server sealed", syn_seen);

    /* the server's packets reach the device through the loop */
    len = pkt_udp4(packet, ABROAD, 443, APP, 5000, (const uint8_t *)"reply", 5);
    size_t wire_len = 0;
    ok("the server seals a reply", awg_tunnel_seal(&server, packet, len, wire, sizeof wire,
                                                    &wire_len) == AWG_TUN_OK);
    send(udp[1], wire, wire_len, 0);
    want_t reply = { 17, 443, 0, 5 };
    got = frame_matching(dev[1], frame, sizeof frame, 3000, is_from_site, &reply);
    ok("the reply comes out of the device", got > 0 && memcmp(frame + 4 + 28, "reply", 5) == 0);

    /* a domain rule decides by what the server's dns answered. 127.0.0.2 is
       named by no ip rule, so it follows the tunnel until site.test resolves to it */
    static const uint8_t SITE_TEST[4] = { 127, 0, 0, 2 };
    len = pkt_udp4(packet, APP, 5102, SITE_TEST, udp_port, (const uint8_t *)"early", 5);
    send_frame(dev[1], packet, len);
    int early_seen = 0;
    until = senko_now_ms() + 2000;
    while (!early_seen && senko_now_ms() < until) {
        got = recv_wait(udp[1], wire, sizeof wire, 200);
        if (got > 0 && awg_tunnel_open(&server, wire, (size_t)got, opened, sizeof opened,
                                       &opened_len) == AWG_TUN_OK && opened_len == len &&
            memcmp(opened, packet, len) == 0)
            early_seen = 1;
    }
    ok("an address no answer named follows the tunnel", early_seen);

    static const uint8_t answer[] = {
        0x12, 0x34, 0x81, 0x80, 0, 1, 0, 1, 0, 0, 0, 0,
        4, 's', 'i', 't', 'e', 4, 't', 'e', 's', 't', 0, 0, 1, 0, 1,
        0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 1, 0x2c, 0, 4, 127, 0, 0, 2
    };
    static const uint8_t DNS_SERVER[4] = { 10, 8, 0, 1 };
    len = pkt_udp4(packet, DNS_SERVER, 53, APP, 5100, answer, sizeof answer);
    ok("the server seals the dns answer", awg_tunnel_seal(&server, packet, len, wire, sizeof wire,
                                                          &wire_len) == AWG_TUN_OK);
    send(udp[1], wire, wire_len, 0);
    want_t dns_out = { 17, 53, 0, sizeof answer };
    got = frame_matching(dev[1], frame, sizeof frame, 3000, is_from_site, &dns_out);
    ok("the answer still reaches the app", got > 0);

    len = pkt_udp4(packet, APP, 5103, SITE_TEST, udp_port, (const uint8_t *)"named", 5);
    send_frame(dev[1], packet, len);
    want_t named = { 17, udp_port, 0, 5 };
    got = frame_matching(dev[1], frame, sizeof frame, 3000, is_from_site, &named);
    ok("an address the answer named goes out locally", got > 0 &&
       memcmp(frame + 4 + 28, "named", 5) == 0);

    /* the link's timers run beside the stack: the persistent keepalive goes out */
    int keepalive = 0;
    until = senko_now_ms() + 3500;
    while (!keepalive && senko_now_ms() < until) {
        got = recv_wait(udp[1], wire, sizeof wire, 300);
        if (got > 0 && awg_tunnel_open(&server, wire, (size_t)got, opened, sizeof opened,
                                       &opened_len) == AWG_TUN_OK && opened_len == 0)
            keepalive = 1;
    }
    ok("the persistent keepalive is sent", keepalive);

    awg_split_counts_t counts;
    awg_split_counts(split, &counts);
    ok("three flows stayed in the tunnel, three went local",
       counts.tunnel_flows == 3 && counts.local_flows == 3);

    char one = 1;
    ok("the stop is written", write(wake[1], &one, 1) == 1);
    pthread_join(loop_thread, NULL);
    ok("the run ends because it was asked", runner.status == AWG_LINK_STOPPED);

    struct sockaddr_in self;
    memset(&self, 0, sizeof self);
    self.sin_family = AF_INET;
    self.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    self.sin_port = htons(udp_port);
    int poke = socket(AF_INET, SOCK_DGRAM, 0);
    sendto(poke, "stop", 4, 0, (struct sockaddr *)&self, sizeof self);
    close(poke);
    pthread_join(site_thread, NULL);
    awg_split_close(split);
    awg_link_clear(&link);
    close(sites.tcp);
    close(sites.udp);
    close(sites.udp2);

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("awg split ok\n");
    return 0;
}
