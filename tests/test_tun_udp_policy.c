/* the rules, dns cache and direct paths of the utun udp relay, the parts that
   replaced the pf dns forwarder and the pf bypass table */
#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "core/dns_msg.h"
#include "core/senko_time.h"
#include "packet_fixture.h"
#include "tun_loop.h"
#include "tun_udp.h"

static int failures;
static const uint8_t APP4[4] = { 198, 18, 0, 1 };
static const uint8_t LOCAL4[4] = { 198, 18, 0, 2 };
static const uint8_t SYNTH4[4] = { 198, 18, 0, 3 };
static const uint8_t RESOLVER4[4] = { 192, 0, 2, 53 };
static const uint8_t UPSTREAM4[4] = { 127, 0, 0, 1 };
static const uint8_t BLOCKED4[4] = { 203, 0, 113, 9 };
static const uint8_t PROXIED4[4] = { 192, 0, 2, 80 };
static const uint8_t LOOP4[4] = { 127, 0, 0, 1 };
static const uint8_t UUID[16] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 };
#define GENERATION 23

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

static int read_full(int fd, uint8_t *out, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = read(fd, out + off, len - off);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static int write_full(int fd, const uint8_t *data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

/* ---- a server that speaks vless udp or socks5 connect -------------------- */

static struct {
    int listen_fd;
    uint16_t port;
    pthread_mutex_t lock;
    int connections;
    int socks_connections;
    int dns_queries;
    int udp_datagrams;
    uint16_t last_dns_port;
} server;

static size_t dns_answer(const uint8_t *q, size_t qlen, uint8_t *out) {
    dns_question_t question;
    if (dns_msg_parse_question(q, qlen, &question) != DNS_MSG_OK) return 0;
    if (strstr(question.name, "silent")) return 0;
    const uint8_t *address = strstr(question.name, "direct") ? LOOP4 : PROXIED4;
    memcpy(out, q, qlen);
    out[2] = 0x81; out[3] = 0x80; out[7] = 1;
    size_t n = qlen;
    const uint8_t rr[] = { 0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0x0e, 0x10, 0, 4 };
    memcpy(out + n, rr, sizeof rr);
    n += sizeof rr;
    memcpy(out + n, address, 4);
    return n + 4;
}

/* two byte length framed datagrams, the vless udp codec and dns over tcp alike */
static void serve_frames(int fd, uint16_t port) {
    int header_sent = 0;
    for (;;) {
        uint8_t length[2];
        if (read_full(fd, length, 2) != 0) return;
        size_t len = pkt_rd16(length);
        uint8_t payload[1500], answer[1600];
        if (len == 0 || len > sizeof payload || read_full(fd, payload, len) != 0) return;
        size_t answer_len = 0;
        pthread_mutex_lock(&server.lock);
        if (port == 53) ++server.dns_queries; else ++server.udp_datagrams;
        pthread_mutex_unlock(&server.lock);
        if (port == 53) {
            answer_len = dns_answer(payload, len, answer);
        } else {
            memcpy(answer, payload, len);
            answer_len = len;
        }
        if (!answer_len) continue;
        uint8_t head[4] = { 0, 0, (uint8_t)(answer_len >> 8), (uint8_t)answer_len };
        const uint8_t *prefix = header_sent || server.socks_connections ? head + 2 : head;
        size_t prefix_len = header_sent || server.socks_connections ? 2u : 4u;
        if (write_full(fd, prefix, prefix_len) != 0 ||
            write_full(fd, answer, answer_len) != 0) return;
        header_sent = 1;
    }
}

static void *serve_one(void *arg) {
    int fd = (int)(intptr_t)arg;
    uint8_t first;
    if (read_full(fd, &first, 1) != 0) { close(fd); return NULL; }
    uint16_t port = 0;
    if (first == 5) {
        /* socks5: greeting, then CONNECT to the dns upstream */
        uint8_t rest[1 + 255], req[10];
        if (read_full(fd, rest, 1) != 0 || read_full(fd, rest + 1, rest[0]) != 0) goto done;
        const uint8_t pick[2] = { 5, 0 };
        if (write_full(fd, pick, 2) != 0 || read_full(fd, req, 10) != 0 || req[3] != 1)
            goto done;
        port = pkt_rd16(req + 8);
        const uint8_t granted[10] = { 5, 0, 0, 1, 0, 0, 0, 0, 0, 0 };
        if (write_full(fd, granted, 10) != 0) goto done;
        pthread_mutex_lock(&server.lock);
        ++server.socks_connections;
        server.last_dns_port = port;
        pthread_mutex_unlock(&server.lock);
    } else {
        uint8_t head[17], addons[255], request[4], address[16];
        if (read_full(fd, head, 17) != 0 || memcmp(head, UUID, 16) != 0 ||
            (head[16] && read_full(fd, addons, head[16]) != 0) ||
            read_full(fd, request, 4) != 0 || request[0] != 2)
            goto done;
        size_t alen = request[3] == 1 ? 4u : 16u;
        if (read_full(fd, address, alen) != 0) goto done;
        port = pkt_rd16(request + 1);
    }
    serve_frames(fd, port);
done:
    close(fd);
    return NULL;
}

static void *server_main(void *ctx) {
    (void)ctx;
    for (;;) {
        int fd = accept(server.listen_fd, NULL, NULL);
        if (fd < 0) return NULL;
        pthread_mutex_lock(&server.lock);
        ++server.connections;
        pthread_mutex_unlock(&server.lock);
        pthread_t thread;
        pthread_create(&thread, NULL, serve_one, (void *)(intptr_t)fd);
        pthread_detach(thread);
    }
}

static int dial_server(void *ctx, char *error, size_t error_cap) {
    (void)ctx;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(server.port);
    if (connect(fd, (struct sockaddr *)&address, sizeof address) == 0) return fd;
    snprintf(error, error_cap, "local test server refused connect");
    close(fd);
    return -1;
}

/* direct traffic in the test just leaves by loopback */
static int plain_socket(void *ctx, int family, int type, char *error, size_t cap) {
    (void)ctx; (void)error; (void)cap;
    int fd = socket(family, type, 0);
    if (fd >= 0) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return fd;
}

/* ---- the tunnel ---------------------------------------------------------- */

typedef struct {
    tun_loop_t *loop;
    tun_udp_t *udp;
    utun_device_t device;
    int peer;
    pthread_t thread;
    tun_loop_result_t result;
} tunnel_t;

static ruleset_t rules;
static tun_policy_t policy;

static void *run_loop(void *ctx) {
    tunnel_t *t = ctx;
    (void)tun_loop_run(t->loop, &t->result);
    return NULL;
}

static void tunnel_start(tunnel_t *t, vl_proto_t proto) {
    int fds[2];
    socketpair(AF_UNIX, SOCK_DGRAM, 0, fds);
    t->peer = fds[1];
    utun_device_init(&t->device);
    utun_device_adopt(&t->device, fds[0], "utun9");
    tun_loop_config_t config;
    memset(&config, 0, sizeof config);
    config.generation = GENERATION;
    config.stack.mtu = 1500;
    config.stack.have_ipv4 = 1;
    memcpy(config.stack.address4, LOCAL4, 4);
    memcpy(config.stack.synthetic4, SYNTH4, 4);
    memset(config.stack.netmask4, 255, 4);
    tun_udp_config_t udp;
    memset(&udp, 0, sizeof udp);
    udp.vt = &transport_tcp;
    udp.proto = proto;
    memcpy(udp.uuid, UUID, sizeof UUID);
    udp.dial = dial_server;
    udp.opener_threads = 1;
    udp.policy = &policy;
    udp.direct_socket = plain_socket;
    memcpy(udp.dns_upstream, UPSTREAM4, 4);
    udp.dns_upstream_len = 4;
    udp.block_response = DNS_BLOCK_NXDOMAIN;
    t->udp = malloc(tun_udp_size());
    t->loop = malloc(tun_loop_size());
    ok("udp relay configured", tun_udp_init(t->udp, &udp, &config) == 0);
    ok("tunnel initialized", tun_loop_init(t->loop, &config, &t->device) == TUN_LOOP_OK);
    tun_udp_bind(t->udp, t->loop, GENERATION);
    pthread_create(&t->thread, NULL, run_loop, t);
}

static void tunnel_stop(tunnel_t *t) {
    tun_loop_command_t stop = { TUN_LOOP_CMD_STOP, GENERATION, 0 };
    tun_loop_post(t->loop, &stop);
    pthread_join(t->thread, NULL);
    tun_loop_destroy(t->loop);
    free(t->loop);
    free(t->udp);
    close(t->peer);
}

static size_t query(uint8_t *out, uint16_t id, const char *name) {
    size_t n = 12;
    memset(out, 0, 12);
    pkt_wr16(out, id); out[2] = 1; out[5] = 1;
    for (const char *p = name; *p;) {
        const char *dot = strchr(p, '.');
        size_t len = dot ? (size_t)(dot - p) : strlen(p);
        out[n++] = (uint8_t)len;
        memcpy(out + n, p, len);
        n += len;
        p += len;
        if (*p) ++p;
    }
    out[n++] = 0;
    out[n++] = 0; out[n++] = 1; out[n++] = 0; out[n++] = 1;
    return n;
}

/* sends one datagram and returns the udp payload of the answer, or 0 */
static size_t exchange(tunnel_t *t, const uint8_t dst[4], uint16_t sport, uint16_t dport,
                       const uint8_t *payload, size_t len, uint8_t *answer,
                       int wait_ms) {
    uint8_t packet[1600], frame[TUN_STACK_FRAME_MAX];
    size_t plen = pkt_udp4(packet, APP4, sport, dst, dport, payload, len);
    size_t flen = pkt_frame(frame, packet, plen);
    if (send(t->peer, frame, flen, 0) != (ssize_t)flen) return 0;
    struct pollfd pfd = { t->peer, POLLIN, 0 };
    if (poll(&pfd, 1, wait_ms) <= 0) return 0;
    ssize_t got = recv(t->peer, frame, sizeof frame, 0);
    if (got < 4 + 28) return 0;
    memcpy(answer, frame + 4 + 28, (size_t)got - 4 - 28);
    return (size_t)got - 4 - 28;
}

static int answered_with(const uint8_t *answer, size_t len, const uint8_t address[4]) {
    return len >= 4 && memcmp(answer + len - 4, address, 4) == 0;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    memset(&server, 0, sizeof server);
    pthread_mutex_init(&server.lock, NULL);
    server.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(server.listen_fd, (struct sockaddr *)&address, sizeof address);
    socklen_t address_len = sizeof address;
    getsockname(server.listen_fd, (struct sockaddr *)&address, &address_len);
    server.port = ntohs(address.sin_port);
    listen(server.listen_fd, 8);
    pthread_t server_thread;
    pthread_create(&server_thread, NULL, server_main, NULL);

    /* a local echo stands in for a site the rules send direct */
    int echo = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in echo_address = address;
    echo_address.sin_port = 0;
    bind(echo, (struct sockaddr *)&echo_address, sizeof echo_address);
    socklen_t echo_len = sizeof echo_address;
    getsockname(echo, (struct sockaddr *)&echo_address, &echo_len);
    uint16_t echo_port = ntohs(echo_address.sin_port);

    ruleset_init(&rules);
    const char *texts[] = { "block domain-suffix ads.test", "direct domain-suffix direct.test",
                            "block ip-cidr 203.0.113.0/24" };
    for (size_t i = 0; i < 3; ++i)
        ruleset_add_text(&rules, texts[i], strlen(texts[i]), NULL);
    ok("policy ready", tun_policy_init(&policy, &rules) == TUN_POLICY_OK);

    tunnel_t vless;
    tunnel_start(&vless, VL_PROTO_VLESS);
    uint8_t q[128], answer[1600];
    size_t qlen, got;

    qlen = query(q, 0x0101, "x.ads.test");
    got = exchange(&vless, RESOLVER4, 50001, 53, q, qlen, answer, 2000);
    ok("a blocked domain is answered on the device",
       got >= 4 && (answer[3] & 0x0f) == 3 && server.dns_queries == 0);

    qlen = query(q, 0x0202, "www.example.test");
    got = exchange(&vless, RESOLVER4, 50002, 53, q, qlen, answer, 3000);
    ok("an unruled domain resolves through the server",
       answered_with(answer, got, PROXIED4) && server.dns_queries == 1);
    qlen = query(q, 0x0203, "www.example.test");
    got = exchange(&vless, RESOLVER4, 50003, 53, q, qlen, answer, 2000);
    ok("the same lookup again comes from the cache with its own id",
       answered_with(answer, got, PROXIED4) && answer[0] == 0x02 && answer[1] == 0x03 &&
       server.dns_queries == 1 && policy.stats.dns_cache_hits == 1);

    qlen = query(q, 0x0303, "echo.direct.test");
    got = exchange(&vless, RESOLVER4, 50004, 53, q, qlen, answer, 3000);
    ok("a direct domain still resolves through the server",
       answered_with(answer, got, LOOP4) && server.dns_queries == 2);
    static const uint8_t ping[] = { 'p', 'i', 'n', 'g' };
    int udp_before = server.udp_datagrams;
    uint8_t buf[64];
    struct sockaddr_in from;
    socklen_t from_len = sizeof from;
    uint8_t packet[1600], frame[TUN_STACK_FRAME_MAX];
    size_t plen = pkt_udp4(packet, APP4, 50005, LOOP4, echo_port, ping, sizeof ping);
    size_t flen = pkt_frame(frame, packet, plen);
    send(vless.peer, frame, flen, 0);
    struct pollfd epfd = { echo, POLLIN, 0 };
    ssize_t echoed = poll(&epfd, 1, 2000) > 0
        ? recvfrom(echo, buf, sizeof buf, 0, (struct sockaddr *)&from, &from_len) : -1;
    ok("udp to the direct domain's address leaves directly",
       echoed == (ssize_t)sizeof ping && server.udp_datagrams == udp_before);
    if (echoed > 0) sendto(echo, "pong", 4, 0, (struct sockaddr *)&from, from_len);
    struct pollfd ppfd = { vless.peer, POLLIN, 0 };
    got = poll(&ppfd, 1, 2000) > 0 ? (size_t)recv(vless.peer, frame, sizeof frame, 0) : 0;
    ok("and its answer comes back to the app from that address",
       got == 4 + 28 + 4 && memcmp(frame + 4 + 28, "pong", 4) == 0 &&
       memcmp(frame + 4 + 12, LOOP4, 4) == 0 && pkt_rd16(frame + 4 + 20) == echo_port);

    got = exchange(&vless, BLOCKED4, 50006, 9999, ping, sizeof ping, answer, 500);
    ok("an ip block rule drops the datagram", got == 0 &&
       tun_udp_stats(vless.udp)->blocked == 1);

    got = exchange(&vless, PROXIED4, 50007, 9999, ping, sizeof ping, answer, 3000);
    ok("other udp goes through the vless server",
       got == sizeof ping && server.udp_datagrams == udp_before + 1);

    /* an upstream that stops answering: a stale cached answer covers it */
    qlen = query(q, 0x0404, "stale.silent.test");
    uint8_t cached[128];
    memcpy(cached, q, qlen);
    cached[2] = 0x81; cached[3] = 0x80; cached[7] = 1;
    const uint8_t rr[] = { 0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 0, 2, 77 };
    memcpy(cached + qlen, rr, sizeof rr);
    uint64_t now = (uint64_t)(senko_now_ms() / 1000);
    dns_cache_put(policy.cache, "stale.silent.test", 1, cached, qlen + sizeof rr,
                  now - 120, 60, RULE_ACTION_PROXY);
    got = exchange(&vless, RESOLVER4, 50008, 53, q, qlen, answer, 4000);
    const uint8_t stale_address[4] = { 192, 0, 2, 77 };
    ok("a slow upstream gets the stale cached answer",
       answered_with(answer, got, stale_address) && policy.stats.dns_stale_answers == 1);
    tunnel_stop(&vless);

    /* a server that only carries tcp: dns over tcp through a socks5 connect */
    tunnel_t socks;
    tunnel_start(&socks, VL_PROTO_SOCKS5);
    int queries_before = server.dns_queries;
    qlen = query(q, 0x0505, "www.other.test");
    got = exchange(&socks, RESOLVER4, 50101, 53, q, qlen, answer, 3000);
    ok("a socks5 server resolves over dns-over-tcp to the upstream port 53",
       answered_with(answer, got, PROXIED4) && server.socks_connections == 1 &&
       server.last_dns_port == 53 && server.dns_queries == queries_before + 1);
    got = exchange(&socks, PROXIED4, 50102, 9999, ping, sizeof ping, answer, 500);
    ok("other udp through a tcp-only server is refused with the reason",
       got == 0 && strstr(tun_udp_stats(socks.udp)->last_error, "only a VLESS or VMess server"));
    tunnel_stop(&socks);

    tun_policy_free(&policy);
    shutdown(server.listen_fd, SHUT_RDWR);
    close(server.listen_fd);
    pthread_join(server_thread, NULL);
    close(echo);
    if (failures) {
        fprintf(stderr, "%d tun_udp policy check(s) failed\n", failures);
        return 1;
    }
    puts("all tun_udp policy checks passed");
    return 0;
}
