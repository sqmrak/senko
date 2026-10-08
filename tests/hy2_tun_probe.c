/* the utun tcp bridge end to end over hysteria2, against the reference
   server. an app speaks tcp by hand through a socketpair that stands in for the
   utun descriptor (the same harness as test_tun_tcp.c); the bridge opens each
   flow as a hysteria2 stream through daemon/hy2_engine.c, and the server
   connects to services in this process. the services listen on 203.0.113.7,
   so tests/hy2_tun_probe.sh runs all of it in a private network namespace.

   env: HY2_PORT HY2_AUTH HY2_CA */

#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "hy2_engine.h"
#include "packet_fixture.h"
#include "tun_loop.h"
#include "tun_tcp.h"

static int failures;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

static const uint8_t APP4[4]   = { 198, 18, 0, 1 };
static const uint8_t LOCAL4[4] = { 198, 18, 0, 2 };
static const uint8_t SYNTH4[4] = { 198, 18, 0, 3 };
static const uint8_t DEST_A[4] = { 203, 0, 113, 7 };
static const uint8_t APP6[16]   = { 0xfd, 0, 0x5e, 0x4b, 0,0,0,0, 0,0,0,0, 0,0,0,1 };
static const uint8_t LOCAL6[16] = { 0xfd, 0, 0x5e, 0x4b, 0,0,0,0, 0,0,0,0, 0,0,0,2 };
static const uint8_t SYNTH6[16] = { 0xfd, 0, 0x5e, 0x4b, 0,0,0,0, 0,0,0,0, 0,0,0,3 };

enum { PORT_ECHO = 7, PORT_CHARGEN = 19, PORT_RESET = 13, PORT_CLOSED = 4999 };
#define CHARGEN_BYTES (256u * 1024u)
#define GENERATION 5

static uint8_t pattern_byte(size_t i) { return (uint8_t)(i * 31u + (i >> 9)); }

static uint32_t fnv(uint32_t h, const uint8_t *d, size_t n) {
    for (size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 16777619u; }
    return h;
}

static void pause_ms(int ms) {
    struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

/* ---- the services the hysteria2 server connects to ----------------------- */

static int write_full(int fd, const uint8_t *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w > 0) { off += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static void *serve_one(void *arg) {
    int fd = (int)(intptr_t)arg;
    struct sockaddr_in local;
    socklen_t len = sizeof local;
    getsockname(fd, (struct sockaddr *)&local, &len);
    uint16_t port = ntohs(local.sin_port);
    uint8_t buf[16384];
    if (port == PORT_RESET) {
        struct linger hard = { 1, 0 };
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &hard, sizeof hard);
        close(fd);
        return NULL;
    }
    if (port == PORT_CHARGEN) {
        for (size_t sent = 0; sent < CHARGEN_BYTES; ) {
            size_t n = CHARGEN_BYTES - sent < sizeof buf ? CHARGEN_BYTES - sent : sizeof buf;
            for (size_t i = 0; i < n; ++i) buf[i] = pattern_byte(sent + i);
            if (write_full(fd, buf, n) != 0) break;
            sent += n;
        }
        shutdown(fd, SHUT_WR);
    }
    for (;;) {
        ssize_t r = read(fd, buf, sizeof buf);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        if (port == PORT_ECHO && write_full(fd, buf, (size_t)r) != 0) break;
    }
    if (port == PORT_ECHO) shutdown(fd, SHUT_WR);
    close(fd);
    return NULL;
}

static void *service_main(void *arg) {
    int listener = (int)(intptr_t)arg;
    for (;;) {
        int fd = accept(listener, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) continue;
            return NULL;
        }
        pthread_t t;
        pthread_create(&t, NULL, serve_one, (void *)(intptr_t)fd);
        pthread_detach(t);
    }
}

static void service_start(uint16_t port) {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(listener, (struct sockaddr *)&a, sizeof a) != 0 || listen(listener, 64) != 0) {
        perror("service bind");
        exit(2);
    }
    pthread_t t;
    pthread_create(&t, NULL, service_main, (void *)(intptr_t)listener);
    pthread_detach(t);
}

/* ---- the tunnel under test ---------------------------------------------- */

static struct {
    tun_loop_t     *loop;
    tun_tcp_t      *tcp;
    utun_device_t   device;
    int             peer;
    pthread_t       thread;
    tun_loop_result_t result;
    hy2_engine_t   *engine;
} tunnel;

static void *run_tunnel(void *arg) {
    (void)arg;
    tun_loop_run(tunnel.loop, &tunnel.result);
    return NULL;
}

static void tunnel_start(void) {
    char error[256];
    uint8_t address[1][16] = {{127, 0, 0, 1}};
    uint8_t length[1] = {4};
    hy2_engine_config_t engine;
    memset(&engine, 0, sizeof engine);
    engine.addresses = address;
    engine.address_lens = length;
    engine.address_count = 1;
    engine.port = (uint16_t)atoi(getenv("HY2_PORT"));
    engine.sni = "localhost";
    engine.peer_host = "localhost";
    engine.auth = getenv("HY2_AUTH");
    engine.ca_file = getenv("HY2_CA");
    engine.connect_timeout_ms = 6000;
    tunnel.engine = hy2_engine_start(&engine, error, sizeof error);
    if (!tunnel.engine) {
        fprintf(stderr, "engine start failed: %s\n", error);
        exit(2);
    }

    int fds[2];
    socketpair(AF_UNIX, SOCK_DGRAM, 0, fds);
    int big = 1 << 20;
    setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &big, sizeof big);
    setsockopt(fds[1], SOL_SOCKET, SO_RCVBUF, &big, sizeof big);
    tunnel.peer = fds[1];
    utun_device_init(&tunnel.device);
    utun_device_adopt(&tunnel.device, fds[0], "utun7");

    tun_loop_config_t config;
    memset(&config, 0, sizeof config);
    config.generation = GENERATION;
    tun_stack_config_t *s = &config.stack;
    s->mtu = 1500;
    s->have_ipv4 = 1;
    memcpy(s->address4, LOCAL4, 4);
    memcpy(s->synthetic4, SYNTH4, 4);
    s->netmask4[0] = s->netmask4[1] = s->netmask4[2] = s->netmask4[3] = 255;
    s->have_ipv6 = 1;
    memcpy(s->address6, LOCAL6, 16);
    memcpy(s->synthetic6, SYNTH6, 16);
    s->queue_max_packets = 16;
    s->pending_max_ms = 3000;

    tun_tcp_config_t tcp;
    memset(&tcp, 0, sizeof tcp);
    tcp.vt = &transport_hy2;
    tcp.proto = VL_PROTO_HYSTERIA2;
    tcp.dial = hy2_engine_dial;
    tcp.dial_ctx = tunnel.engine;
    tcp.opener_threads = 2;
    tcp.max_flows = 8;
    tunnel.tcp = malloc(tun_tcp_size());
    tun_tcp_init(tunnel.tcp, &tcp, &config);
    tunnel.loop = malloc(tun_loop_size());
    tun_loop_init(tunnel.loop, &config, &tunnel.device);
    tun_tcp_bind(tunnel.tcp, tunnel.loop, GENERATION);
    pthread_create(&tunnel.thread, NULL, run_tunnel, NULL);
}

/* the engine outlives the loop, as utun_backend_stop orders it */
static void tunnel_stop(void) {
    hy2_engine_cancel(tunnel.engine);
    tun_loop_command_t stop = { TUN_LOOP_CMD_STOP, GENERATION, 0 };
    tun_loop_post(tunnel.loop, &stop);
    pthread_join(tunnel.thread, NULL);
    tun_loop_destroy(tunnel.loop);
    hy2_engine_stop(tunnel.engine);
    close(tunnel.peer);
}

/* ---- an app speaking tcp by hand ---------------------------------------- */

#define APP_RX_MAX (512u * 1024u)
#define MSS 1300

typedef struct {
    int      v6;
    uint8_t  src[16];
    uint8_t  dst[16];
    uint16_t sport;
    uint16_t dport;
    uint32_t snd_nxt;
    uint32_t snd_una;
    uint32_t rcv_nxt;
    uint32_t peer_window;
    uint16_t window;      /* what this app advertises */
    int      established;
    int      got_fin;
    int      got_rst;
    int      syn_acked;
    size_t   rx_len;      /* bytes received in order */
    uint32_t rx_hash;
    int      keep_rx;
    uint8_t *rx;          /* the bytes themselves, when keep_rx */
} app_t;

#define APPS_MAX (TUN_TCP_DEFAULT_MAX_FLOWS + 2)
static app_t *apps[APPS_MAX];
static size_t app_count;

static size_t build(const app_t *a, uint8_t *p, uint8_t flags, const uint8_t *data,
                    size_t len) {
    size_t total;
    if (a->v6) {
        total = pkt_ip6(p, a->src, a->dst, 6, 20 + len, 0, 0);
    } else {
        total = pkt_ip4(p, a->src, a->dst, 6, 20 + len);
    }
    uint8_t *t = p + pkt_l4(p, total);
    pkt_tcp_fields(t, a->sport, a->dport, flags, a->snd_nxt, a->rcv_nxt, 0);
    pkt_wr16(t + 14, a->window);
    if (len) memcpy(t + 20, data, len);
    pkt_fill_l4(p, total, 16);
    return total;
}

static void app_send_raw(app_t *a, uint8_t flags, const uint8_t *data, size_t len) {
    uint8_t p[1600], frame[1604];
    size_t n = build(a, p, flags, data, len);
    size_t f = pkt_frame(frame, p, n);
    send(tunnel.peer, frame, f, 0);
}

static void app_ack(app_t *a) { app_send_raw(a, PKT_ACK, NULL, 0); }

static app_t *match(const uint8_t *p, size_t len) {
    size_t l4 = pkt_l4(p, len);
    int v6 = !pkt_is_v4(p);
    size_t alen = v6 ? 16 : 4;
    uint16_t sport = pkt_rd16(p + l4), dport = pkt_rd16(p + l4 + 2);
    for (size_t i = 0; i < app_count; ++i) {
        app_t *a = apps[i];
        if (!a || a->v6 != v6 || a->sport != dport || a->dport != sport) continue;
        if (memcmp(pkt_src(p), a->dst, alen) != 0) continue;
        return a;
    }
    return NULL;
}

/* read every frame that is there, or wait up to wait_ms for the first one */
static int pump(int wait_ms) {
    int seen = 0;
    for (;;) {
        struct pollfd pfd = { tunnel.peer, POLLIN, 0 };
        if (poll(&pfd, 1, seen ? 0 : wait_ms) <= 0) return seen;
        uint8_t frame[2048];
        ssize_t got = recv(tunnel.peer, frame, sizeof frame, 0);
        if (got <= 4) return seen;
        ++seen;
        const uint8_t *p = frame + 4;
        size_t len = (size_t)got - 4;
        if (pkt_proto(p, len) != 6 || !pkt_l4_valid(p, len)) continue;
        app_t *a = match(p, len);
        if (!a) continue;
        const uint8_t *t = p + pkt_l4(p, len);
        uint8_t flags = t[13];
        uint32_t seq = pkt_rd32(t + 4), ack = pkt_rd32(t + 8);
        size_t hl = (size_t)(t[12] >> 4) * 4;
        size_t data = len - pkt_l4(p, len) - hl;
        if (flags & PKT_RST) { a->got_rst = 1; continue; }
        if ((flags & PKT_SYN) && (flags & PKT_ACK)) {
            a->rcv_nxt = seq + 1;
            a->snd_una = ack;
            a->peer_window = pkt_rd16(t + 14);
            a->syn_acked = 1;
            continue;
        }
        if (flags & PKT_ACK) {
            if ((int32_t)(ack - a->snd_una) > 0) a->snd_una = ack;
            a->peer_window = pkt_rd16(t + 14);
        }
        int advanced = 0;
        if (data && seq == a->rcv_nxt) {
            /* an app with a small window only takes what it advertised */
            size_t take = data;
            if (a->keep_rx && a->rx_len + take > APP_RX_MAX) take = APP_RX_MAX - a->rx_len;
            if (a->keep_rx) memcpy(a->rx + a->rx_len, t + hl, take);
            a->rx_hash = fnv(a->rx_hash, t + hl, take);
            a->rx_len += take;
            a->rcv_nxt += (uint32_t)take;
            advanced = 1;
        }
        if ((flags & PKT_FIN) && seq + (uint32_t)data == a->rcv_nxt) {
            a->rcv_nxt += 1;
            a->got_fin = 1;
            advanced = 1;
        }
        if (advanced || data) app_ack(a);
    }
}

static int wait_until(int *flag, int wait_ms) {
    for (int waited = 0; !*flag && waited < wait_ms; waited += 10) pump(10);
    return *flag;
}

static void app_init(app_t *a, int v6, const uint8_t *dst, uint16_t dport, uint16_t sport) {
    memset(a, 0, sizeof *a);
    a->v6 = v6;
    memcpy(a->src, v6 ? APP6 : APP4, v6 ? 16 : 4);
    memcpy(a->dst, dst, v6 ? 16 : 4);
    a->dport = dport;
    a->sport = sport;
    a->snd_nxt = 1000u * sport;
    a->window = 65535;
    a->rx_hash = 2166136261u;
    for (size_t i = 0; i < APPS_MAX; ++i)
        if (!apps[i]) { apps[i] = a; if (i + 1 > app_count) app_count = i + 1; break; }
}

static void app_forget(app_t *a) {
    for (size_t i = 0; i < APPS_MAX; ++i) if (apps[i] == a) apps[i] = NULL;
    free(a->rx);
    a->rx = NULL;
}

static int app_connect(app_t *a, const uint8_t *early, size_t early_len) {
    app_send_raw(a, PKT_SYN, NULL, 0);
    a->snd_nxt += 1;
    if (!wait_until(&a->syn_acked, 3000)) return 0;
    if (early_len) {
        app_send_raw(a, PKT_ACK | 0x08, early, early_len);
        a->snd_nxt += (uint32_t)early_len;
    } else {
        app_ack(a);
    }
    a->established = 1;
    return 1;
}

/* send len bytes of the pattern starting at offset, respecting the window */
static int app_send(app_t *a, size_t offset, size_t len, int wait_ms) {
    uint8_t chunk[MSS];
    size_t sent = 0;
    int idle = 0;
    while (sent < len) {
        uint32_t in_flight = a->snd_nxt - a->snd_una;
        if (in_flight >= a->peer_window) {
            if (!pump(20)) idle += 20;
            if (idle > wait_ms) return 0;
            continue;
        }
        idle = 0;
        size_t room = a->peer_window - in_flight;
        size_t n = len - sent;
        if (n > MSS) n = MSS;
        if (n > room) n = room;
        for (size_t i = 0; i < n; ++i) chunk[i] = pattern_byte(offset + sent + i);
        app_send_raw(a, PKT_ACK | 0x08, chunk, n);
        a->snd_nxt += (uint32_t)n;
        sent += n;
        pump(0);
    }
    /* wait for everything to be acknowledged */
    for (int waited = 0; a->snd_una != a->snd_nxt && waited < wait_ms; waited += 10) pump(10);
    return a->snd_una == a->snd_nxt;
}

static void app_fin(app_t *a) {
    app_send_raw(a, PKT_FIN | PKT_ACK, NULL, 0);
    a->snd_nxt += 1;
}

static uint32_t pattern_hash(size_t offset, size_t len) {
    uint32_t h = 2166136261u;
    uint8_t chunk[4096];
    for (size_t done = 0; done < len; ) {
        size_t n = len - done < sizeof chunk ? len - done : sizeof chunk;
        for (size_t i = 0; i < n; ++i) chunk[i] = pattern_byte(offset + done + i);
        h = fnv(h, chunk, n);
        done += n;
    }
    return h;
}


static size_t echo_flow(app_t *a, uint16_t sport, size_t bytes, const char *name) {
    app_init(a, 0, DEST_A, PORT_ECHO, sport);
    a->keep_rx = 1;
    a->rx = malloc(APP_RX_MAX);
    ok(name, app_connect(a, NULL, 0));
    ok("upload through hysteria2", app_send(a, 0, bytes, 8000));
    return bytes;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    if (!getenv("HY2_PORT") || !getenv("HY2_AUTH") || !getenv("HY2_CA")) {
        fprintf(stderr, "set HY2_PORT HY2_AUTH HY2_CA\n");
        return 2;
    }
    service_start(PORT_ECHO);
    service_start(PORT_CHARGEN);
    service_start(PORT_RESET);
    tunnel_start();
    static app_t a, flows[6];

    /* one flow: the echo comes back whole and both ends see a clean close */
    size_t bytes = echo_flow(&a, 40001, 100000, "an app connects through hysteria2");
    for (int w = 0; a.rx_len < bytes && w < 8000; w += 10) pump(10);
    ok("the echo comes back whole", a.rx_len == bytes && a.rx_hash == pattern_hash(0, bytes));
    app_fin(&a);
    ok("the app's fin is answered with the server's fin", wait_until(&a.got_fin, 5000));
    ok("a clean close is not a reset", !a.got_rst);
    app_forget(&a);

    /* a download larger than the stream window */
    app_init(&a, 0, DEST_A, PORT_CHARGEN, 40002);
    a.keep_rx = 1;
    a.rx = malloc(APP_RX_MAX);
    ok("connect to the download", app_connect(&a, NULL, 0));
    for (int w = 0; a.rx_len < CHARGEN_BYTES && w < 15000; w += 10) pump(10);
    ok("the download arrives whole", a.rx_len == CHARGEN_BYTES &&
       a.rx_hash == pattern_hash(0, CHARGEN_BYTES));
    ok("the download ends with a fin", wait_until(&a.got_fin, 5000));
    app_fin(&a);
    app_forget(&a);

    /* several flows at once share one quic connection */
    for (size_t i = 0; i < 6; ++i) {
        app_init(&flows[i], 0, DEST_A, PORT_ECHO, (uint16_t)(40010 + i));
        flows[i].keep_rx = 1;
        flows[i].rx = malloc(APP_RX_MAX);
        ok("a concurrent flow connects", app_connect(&flows[i], NULL, 0));
    }
    for (size_t i = 0; i < 6; ++i)
        ok("a concurrent flow uploads", app_send(&flows[i], i * 1000, 20000 + i * 3000, 8000));
    for (size_t i = 0; i < 6; ++i) {
        size_t want = 20000 + i * 3000;
        for (int w = 0; flows[i].rx_len < want && w < 8000; w += 10) pump(10);
        ok("a concurrent echo is whole and in order", flows[i].rx_len == want &&
           flows[i].rx_hash == pattern_hash(i * 1000, want));
    }
    for (size_t i = 0; i < 6; ++i) app_fin(&flows[i]);
    for (size_t i = 0; i < 6; ++i) {
        ok("a concurrent flow closes", wait_until(&flows[i].got_fin, 5000));
        app_forget(&flows[i]);
    }

    /* the server cannot reach the destination: the app gets a reset */
    app_init(&a, 0, DEST_A, PORT_CLOSED, 40030);
    app_connect(&a, NULL, 0);
    ok("a refused destination resets the app", wait_until(&a.got_rst, 5000));
    app_forget(&a);

    /* the destination resets the connection. the reference server ends the quic
       stream with a plain fin, and the protocol has no frame that says more, so
       the app sees an end of stream and no data, never a hang */
    app_init(&a, 0, DEST_A, PORT_RESET, 40031);
    app_connect(&a, NULL, 0);
    for (int w = 0; !a.got_rst && !a.got_fin && w < 5000; w += 10) pump(10);
    ok("a destination reset ends the flow", a.got_rst || (a.got_fin && a.rx_len == 0));
    app_forget(&a);

    /* the tunnel still works after those failures */
    bytes = echo_flow(&a, 40040, 5000, "the tunnel carries a flow after failures");
    for (int w = 0; a.rx_len < bytes && w < 8000; w += 10) pump(10);
    ok("the echo after failures is whole", a.rx_len == bytes && a.rx_hash == pattern_hash(0, bytes));
    app_forget(&a);

    tunnel_stop();
    if (failures) return 1;
    puts("hy2 tun ok");
    return 0;
}
