#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "reality_handshake.h"
#include "vless.h"
#include "vless_conn.h"

static int hex2bin(const char *h, uint8_t *out, size_t cap) {
    size_t n = strlen(h);
    if (n % 2 || n / 2 > cap) return -1;
    for (size_t i = 0; i < n / 2; i++) {
        unsigned v; sscanf(h + 2 * i, "%2x", &v); out[i] = (uint8_t)v;
    }
    return (int)(n / 2);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0); /* unbuffered: survive kill -9, show progress */
    if (argc < 9) {
        fprintf(stderr, "usage: %s sip sport pbkhex sni tip tport path uuid\n", argv[0]);
        return 2;
    }
    const char *sip = argv[1]; int sport = atoi(argv[2]);
    uint8_t pbk[32];
    if (hex2bin(argv[3], pbk, sizeof pbk) != 32) { fprintf(stderr, "bad pbk\n"); return 2; }
    const char *sni = argv[4];
    const char *tip = argv[5]; int tport = atoi(argv[6]);
    const char *path = argv[7]; const char *uuid_s = argv[8];

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(sport); a.sin_addr.s_addr = inet_addr(sip);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) { perror("connect"); return 1; }
    printf("tcp connected to %s:%d\n", sip, sport);

    rh_params_t p; memset(&p, 0, sizeof p);
    p.sni = sni;
    memcpy(p.pbk, pbk, 32);
    p.short_id_len = 0; /* empty short_id (server has "") */
    p.version[0] = 26; p.version[1] = 6; p.version[2] = 27;

    rh_status_t err;
    void *h = reality_handshake_open(fd, &p, &err);
    if (!h) { fprintf(stderr, "reality_handshake_open FAILED rc=%d\n", err); return 1; }
    printf("REALITY HANDSHAKE OK\n");

    uint8_t uuid[16];
    if (vless_uuid_parse(uuid_s, uuid) != VLESS_OK) { fprintf(stderr, "bad uuid\n"); return 1; }

    vless_conn_t vc; memset(&vc, 0, sizeof vc);
    memcpy(vc.uuid, uuid, 16);
    vc.dest.atyp = VLESS_ADDR_IPV4;
    vc.dest.port = (uint16_t)tport;
    {
        unsigned long ip = inet_addr(tip);
        memcpy(vc.dest.host_addr, &ip, 4);
    }
    vc.state = VC_ST_INIT;

    char getbuf[256];
    int glen = snprintf(getbuf, sizeof getbuf, "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, tip);

    uint8_t req[512]; size_t reqlen = 0;
    if (vless_conn_make_request(&vc, (const uint8_t *)getbuf, (size_t)glen,
                                req, sizeof req, &reqlen) != VC_OK) {
        fprintf(stderr, "make_request failed\n"); return 1;
    }
    printf("vless request %zu bytes, writing through reality channel\n", reqlen);

/* the transport vtable read/write are static; reach them via the boxed handle through the public symbol */
    extern const transport_vt_t transport_reality;
    int w = transport_reality.write(h, req, reqlen);
    printf("wrote %d\n", w);

/* read the response */
    uint8_t resp[4096];
    int total = 0, first = 1, idle = 0;
    for (;;) {
        int n = transport_reality.read(h, resp, sizeof resp);
        if (n == TRANSPORT_WANT_READ || n == TRANSPORT_WANT_WRITE) {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = (n == TRANSPORT_WANT_WRITE) ? POLLOUT : POLLIN;
            if (poll(&pfd, 1, 5000) <= 0) { if (++idle) break; } /* 5s no data: give up */
            continue;
        }
        if (n <= 0) break; /* real eof or error */
        idle = 0;
        if (first) {
            printf("response first %d bytes, vless hdr included\n", n);
            first = 0;
        }
        fwrite(resp, 1, (size_t)n, stdout);
        total += n;
        if (total > 8192) break;
    }
    printf("\ntotal %d bytes\n", total);
    transport_reality.close(h);
    return 0;
}
