#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int socks5_connect(const char *host, uint16_t port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return -1; }

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    a.sin_port = htons(11080);
    if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) {
        perror("connect 11080");
        close(s);
        return -1;
    }

    unsigned char greet[] = { 0x05, 0x01, 0x00 };
    if (write(s, greet, 3) != 3) { perror("greet"); close(s); return -1; }
    unsigned char gr[2];
    if (read(s, gr, 2) != 2 || gr[0] != 0x05 || gr[1] != 0x00) {
        fprintf(stderr, "greet resp %02x %02x\n", gr[0], gr[1]);
        close(s);
        return -1;
    }

    size_t hlen = strlen(host);
    unsigned char req[4 + 256 + 2];
    size_t n = 0;
    req[n++] = 0x05;
    req[n++] = 0x01;
    req[n++] = 0x00;
    req[n++] = 0x03;
    req[n++] = (unsigned char)hlen;
    memcpy(req + n, host, hlen);
    n += hlen;
    req[n++] = (unsigned char)(port >> 8);
    req[n++] = (unsigned char)(port & 0xff);
    if (write(s, req, n) != (ssize_t)n) { perror("req"); close(s); return -1; }

    unsigned char rh[4];
    if (read(s, rh, 4) != 4) { perror("rhdr"); close(s); return -1; }
    fprintf(stderr, "socks reply rep=%02x atyp=%02x\n", rh[1], rh[3]);
    if (rh[1] != 0x00) { close(s); return -1; }

    size_t tail = 0;
    if (rh[3] == 0x01) tail = 6;
    else if (rh[3] == 0x04) tail = 18;
    else if (rh[3] == 0x03) {
        unsigned char dlen;
        if (read(s, &dlen, 1) != 1) { close(s); return -1; }
        tail = (size_t)dlen + 2;
    }
    while (tail > 0) {
        unsigned char junk[64];
        size_t chunk = tail > sizeof junk ? sizeof junk : tail;
        if (read(s, junk, chunk) != (ssize_t)chunk) { close(s); return -1; }
        tail -= chunk;
    }
    return s;
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "example.com";
    uint16_t port = (uint16_t)(argc > 2 ? atoi(argv[2]) : 80);

    int s = socks5_connect(host, port);
    if (s < 0) return 1;

    char msg[256];
    snprintf(msg, sizeof msg,
             "GET / HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", host);
    if (write(s, msg, strlen(msg)) < 0) { perror("write http"); close(s); return 1; }

    char buf[512];
    ssize_t n = read(s, buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = '\0';
        fputs(buf, stderr);
    } else {
        perror("read http");
    }
    close(s);
    return n > 0 ? 0 : 1;
}
