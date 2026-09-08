/* drive the websocket transport past a real handshake, then feed fuzzed server
   frames through its read path over a socketpair */
#include "transport.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/evp.h>

static void nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static int b64(const uint8_t *in, size_t len, char *out, size_t cap) {
    static const char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t j = 0;
    if (cap < ((len + 2) / 3) * 4 + 1) return -1;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = ((uint32_t)in[i] << 16) |
                       ((i + 1 < len ? (uint32_t)in[i + 1] : 0) << 8) |
                        (i + 2 < len ? (uint32_t)in[i + 2] : 0);
        out[j++] = chars[(val >> 18) & 0x3f];
        out[j++] = chars[(val >> 12) & 0x3f];
        out[j++] = (i + 1 < len) ? chars[(val >> 6) & 0x3f] : '=';
        out[j++] = (i + 2 < len) ? chars[val & 0x3f] : '=';
    }
    out[j] = '\0';
    return 0;
}

static int key_from_request(const char *req, char *out, size_t cap) {
    const char *p = strstr(req, "\r\nSec-WebSocket-Key:");
    if (!p) return -1;
    p += strlen("\r\nSec-WebSocket-Key:");
    while (*p == ' ' || *p == '\t') ++p;
    const char *e = strstr(p, "\r\n");
    if (!e) return -1;
    size_t n = (size_t)(e - p);
    if (n + 1 > cap) return -1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

static int accept_for_key(const char *key, char *out, size_t cap) {
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char input[96];
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    int n = snprintf(input, sizeof input, "%s%s", key, guid);
    if (n < 0 || (size_t)n >= sizeof input) return -1;
    if (EVP_Digest(input, (size_t)n, digest, &digest_len, EVP_sha1(), NULL) != 1)
        return -1;
    if (digest_len != 20) return -1;
    return b64(digest, digest_len, out, cap);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int sv[2];
    char req[2048];
    char key[64];
    char accept[64];
    char resp[256];
    uint8_t scratch[8];

    if (size > 65536) return 0;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) return 0;
    nonblock(sv[0]);
    nonblock(sv[1]);

    transport_tls_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.sni = "tls.example";
    cfg.ws_host = "cdn.example";
    cfg.path = "/ws";

    void *h = transport_ws_tcp.open(sv[0], &cfg);
    if (!h) { close(sv[0]); close(sv[1]); return 0; }

    uint8_t one = 0;
    (void)transport_ws_tcp.write(h, &one, 1);

    ssize_t n = read(sv[1], req, sizeof req - 1);
    if (n > 0) {
        req[n] = '\0';
        if (key_from_request(req, key, sizeof key) == 0 &&
            accept_for_key(key, accept, sizeof accept) == 0) {
            int rn = snprintf(resp, sizeof resp,
                              "HTTP/1.1 101 Switching Protocols\r\n"
                              "Upgrade: websocket\r\n"
                              "Connection: Upgrade\r\n"
                              "Sec-WebSocket-Accept: %s\r\n"
                              "\r\n", accept);
            if (rn > 0 && (size_t)rn < sizeof resp &&
                write(sv[1], resp, (size_t)rn) == rn) {
                (void)transport_ws_tcp.read(h, scratch, sizeof scratch);

                /* feed the fuzzed frame bytes in varying slices and drain */
                size_t off = 0;
                int idle = 0;
                while (off < size && idle < 64) {
                    size_t chunk = (size_t)(data[off] % 251) + 1;
                    if (chunk > size - off) chunk = size - off;
                    ssize_t w = write(sv[1], data + off, chunk);
                    if (w > 0) off += (size_t)w;
                    else idle++;

                    uint8_t out[4096];
                    size_t want = (size_t)(data[off < size ? off : size - 1] % 64) * 61 + 1;
                    if (want > sizeof out) want = sizeof out;
                    int r = transport_ws_tcp.read(h, out, want);
                    if (r == TRANSPORT_EOF || (r < 0 && r != TRANSPORT_WANT_READ &&
                                               r != TRANSPORT_WANT_WRITE))
                        break;
                }
                for (int i = 0; i < 8; ++i) {
                    uint8_t out[4096];
                    int r = transport_ws_tcp.read(h, out, sizeof out);
                    if (r == TRANSPORT_EOF || (r < 0 && r != TRANSPORT_WANT_READ &&
                                               r != TRANSPORT_WANT_WRITE))
                        break;
                }
            }
        }
    }

    transport_ws_tcp.close(h);
    close(sv[0]);
    close(sv[1]);
    return 0;
}
