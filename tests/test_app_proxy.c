#define _DEFAULT_SOURCE

#include "../senkotlsfix/stl_proxy.h"
#include "../senkotlsfix/fishhook.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define STATE_PATH "/tmp/senko-c-proxy-test"

static int fails;
static int listener;
static int server_ok;

static void ok(const char *name, int pass) {
    if (!pass) { fprintf(stderr, "FAIL: %s\n", name); fails++; }
}

int rebind_symbols(struct rebinding rebindings[], size_t count) {
    (void)rebindings;
    (void)count;
    return 0;
}

void stl_log(const char *fmt, ...) {
    (void)fmt;
}

static int exact_read(int fd, uint8_t *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = read(fd, buf + off, len - off);
        if (n <= 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

static void *fake_socks(void *unused) {
    (void)unused;
    int fd = accept(listener, NULL, NULL);
    uint8_t buf[22];
    if (fd >= 0 && exact_read(fd, buf, 3) == 0 &&
        memcmp(buf, "\x05\x01\x00", 3) == 0) {
        const uint8_t greet[] = { 5, 0 };
        (void)write(fd, greet, sizeof greet);
        if (exact_read(fd, buf, 10) == 0 && buf[0] == 5 && buf[1] == 1 &&
            buf[3] == 1 && memcmp(buf + 4, "\x08\x08\x04\x04", 4) == 0 &&
            buf[8] == 1 && buf[9] == 187) {
            const uint8_t reply[] = { 5, 0, 0, 1, 127, 0, 0, 1, 0x2b, 0x48 };
            if (write(fd, reply, sizeof reply) == (ssize_t)sizeof reply)
                server_ok = 1;
        }
    }
    if (fd >= 0) close(fd);
    return NULL;
}

int main(void) {
    unlink(STATE_PATH);
    listener = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in local;
    memset(&local, 0, sizeof local);
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok("bind fake socks", bind(listener, (struct sockaddr *)&local, sizeof local) == 0);
    ok("listen fake socks", listen(listener, 1) == 0);
    socklen_t local_len = sizeof local;
    ok("read fake socks port", getsockname(listener, (struct sockaddr *)&local, &local_len) == 0);

    FILE *state = fopen(STATE_PATH, "w");
    ok("create root-owned state", state != NULL);
    if (state) {
        fprintf(state, "SENKO-C-PROXY-V1 %u\n", (unsigned)ntohs(local.sin_port));
        fclose(state);
    }
    chmod(STATE_PATH, 0644);
    ok("validated state", stl_proxy_active_port_for_test() == ntohs(local.sin_port));
    chmod(STATE_PATH, 0666);
    ok("writable state rejected", stl_proxy_active_port_for_test() == 0);
    chmod(STATE_PATH, 0644);

    pthread_t thread;
    ok("start fake socks", pthread_create(&thread, NULL, fake_socks, NULL) == 0);
    int client = socket(AF_INET, SOCK_STREAM, 0);
    int flags = fcntl(client, F_GETFL, 0);
    ok("set nonblocking", flags >= 0 && fcntl(client, F_SETFL, flags | O_NONBLOCK) == 0);
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof dest);
    dest.sin_family = AF_INET;
    dest.sin_port = htons(443);
    inet_pton(AF_INET, "8.8.4.4", &dest.sin_addr);
    ok("public tcp tunneled", stl_proxy_connect_for_test(
        client, (const struct sockaddr *)&dest, sizeof dest) == 0);
    ok("nonblocking restored", (fcntl(client, F_GETFL, 0) & O_NONBLOCK) != 0);
    pthread_join(thread, NULL);
    ok("socks destination preserved", server_ok == 1);

    close(client);
    close(listener);
    unlink(STATE_PATH);
    if (fails) return 1;
    printf("all application proxy checks passed\n");
    return 0;
}
