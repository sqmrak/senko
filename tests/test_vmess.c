#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmess_client.h"

#include "vmess_fixture.inc"
#include "vmess_udp_fixture.inc"

static int failed;

static void ok(const char *name, int cond) {
    if (cond) return;
    fprintf(stderr, "FAIL %s\n", name);
    failed = 1;
}

/* the generator the fixture requests were built with */
static uint8_t counter;
static uint8_t counter_start;
static int fixed_random(uint8_t *out, size_t len) {
    for (size_t i = 0; i < len; ++i) out[i] = (uint8_t)(counter++ * 37 + 11);
    return 0;
}

static const char k_uuid[] = "b831381d-6324-4d53-ad4f-8cda48b30811";

static vmess_client_t *make_client(const char *cipher, const vless_dest_t *dest) {
    uint8_t uuid[VLESS_UUID_LEN];
    vmess_client_t *c = calloc(1, sizeof *c);
    counter = counter_start;
    if (!c || vless_uuid_parse(k_uuid, uuid) != VLESS_OK ||
        vmess_client_init(c, uuid, cipher, dest, fixed_random) != VMESS_OK) {
        free(c);
        return NULL;
    }
    c->unix_time = vmess_fixture_time;
    return c;
}

static vless_dest_t domain_dest(void) {
    vless_dest_t d;
    memset(&d, 0, sizeof d);
    d.atyp = VLESS_ADDR_DOMAIN;
    strcpy(d.domain, "example.com");
    d.port = 443;
    return d;
}

static vless_dest_t ipv4_dest(void) {
    vless_dest_t d;
    memset(&d, 0, sizeof d);
    d.atyp = VLESS_ADDR_IPV4;
    d.host_addr[0] = 1;
    d.host_addr[1] = 2;
    d.host_addr[2] = 3;
    d.host_addr[3] = 4;
    d.port = 80;
    return d;
}

static void test_kdf(void) {
    uint8_t key[16], id[16], nonce[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }, out[32];
    for (int i = 0; i < 16; ++i) {
        key[i] = (uint8_t)i;
        id[i] = (uint8_t)(0xA0 + i);
    }
    ok("kdf without a path", vmess_kdf(key, 16, NULL, NULL, 0, out) == 0 &&
       memcmp(out, vmess_kdf_empty, 32) == 0);
    const uint8_t *one[] = { (const uint8_t *)"AES Auth ID Encryption" };
    size_t one_len[] = { 22 };
    ok("kdf with one label", vmess_kdf(key, 16, one, one_len, 1, out) == 0 &&
       memcmp(out, vmess_kdf_auth_id, 32) == 0);
    const uint8_t *three[] = { (const uint8_t *)"VMess Header AEAD Key", id, nonce };
    size_t three_len[] = { 21, 16, 8 };
    ok("kdf with label, auth id and nonce", vmess_kdf(key, 16, three, three_len, 3, out) == 0 &&
       memcmp(out, vmess_kdf_header_key, 32) == 0);
    const uint8_t *four[] = { id, id, id, id };
    size_t four_len[] = { 16, 16, 16, 16 };
    ok("kdf refuses a path deeper than vmess uses",
       vmess_kdf(key, 16, four, four_len, 4, out) != 0);
}

/* feeds the response in uneven pieces so headers and chunks split everywhere */
static int drain(vmess_client_t *c, const uint8_t *wire, size_t len, size_t step,
                 uint8_t *plain, size_t plain_cap, size_t *got) {
    size_t off = 0;
    *got = 0;
    int last = VMESS_NEED_MORE;
    while (off < len || last == VMESS_OK) {
        size_t take = len - off < step ? len - off : step;
        size_t n = 0;
        last = vmess_client_feed_downstream(c, take ? wire + off : NULL, take,
                                            plain + *got, plain_cap - *got, &n);
        off += take;
        *got += n;
        if (last == VMESS_EOF) return VMESS_EOF;
        if (last != VMESS_OK && last != VMESS_NEED_MORE) return last;
        if (!take && last != VMESS_OK) break;
    }
    return last;
}

static void test_aes_fixture(void) {
    vless_dest_t d = domain_dest();
    vmess_client_t *c = make_client("aes-128-gcm", &d);
    ok("aes client", c != NULL);
    if (!c) return;
    static const char get[] = "GET / HTTP/1.1\r\n\r\n";
    uint8_t req[1024];
    size_t n = 0;
    ok("aes request builds", vmess_client_build_request(c, (const uint8_t *)get, sizeof get - 1,
                                                       req, sizeof req, &n) == VMESS_OK);
    ok("aes request is the bytes xray opened",
       n == sizeof vmess_aes_request && memcmp(req, vmess_aes_request, n) == 0);
    ok("a second header is refused",
       vmess_client_build_request(c, NULL, 0, req, sizeof req, &n) == VMESS_ERR_ARG);

    static uint8_t plain[16384];
    size_t got = 0;
    int rc = drain(c, vmess_aes_response, sizeof vmess_aes_response, 7, plain, sizeof plain, &got);
    char want[19 + 9000 + 4];
    memcpy(want, "HTTP/1.1 200 OK\r\n\r\n", 19);
    for (int i = 0; i < 9000; ++i) want[19 + i] = (char)('a' + i % 26);
    memcpy(want + 19 + 9000, "tail", 4);
    ok("aes response reaches the end chunk", rc == VMESS_EOF);
    ok("aes response payload", got == sizeof want && memcmp(plain, want, got) == 0);
    size_t more = 0;
    ok("eof stays eof", vmess_client_feed_downstream(c, NULL, 0, plain, sizeof plain, &more) ==
       VMESS_EOF && more == 0);
    free(c);
}

static void test_chacha_fixture(void) {
    vless_dest_t d = ipv4_dest();
    vmess_client_t *c = make_client("chacha20-poly1305", &d);
    ok("chacha client", c != NULL);
    if (!c) return;
    uint8_t req[1024];
    size_t n = 0;
    ok("chacha request builds", vmess_client_build_request(c, NULL, 0, req, sizeof req, &n) == VMESS_OK);
    ok("chacha request is the bytes xray opened",
       n == sizeof vmess_chacha_request && memcmp(req, vmess_chacha_request, n) == 0);
    uint8_t plain[64];
    size_t got = 0;
    int rc = drain(c, vmess_chacha_response, sizeof vmess_chacha_response,
                   sizeof vmess_chacha_response, plain, sizeof plain, &got);
    ok("chacha response", rc == VMESS_EOF && got == 4 && memcmp(plain, "pong", 4) == 0);
    free(c);
}

/* an output buffer smaller than a chunk leaves it staged instead of advancing */
static void test_small_output(void) {
    vless_dest_t d = domain_dest();
    vmess_client_t *c = make_client("aes-128-gcm", &d);
    if (!c) return;
    uint8_t req[1024];
    size_t n = 0;
    (void)vmess_client_build_request(c, (const uint8_t *)"GET / HTTP/1.1\r\n\r\n", 18,
                                     req, sizeof req, &n);
    uint8_t small[8];
    size_t got = 0;
    int rc = vmess_client_feed_downstream(c, vmess_aes_response, sizeof vmess_aes_response,
                                          small, sizeof small, &got);
    ok("a chunk larger than the output stays staged", rc == VMESS_NEED_MORE && got == 0);
    static uint8_t big[16384];
    rc = vmess_client_feed_downstream(c, NULL, 0, big, sizeof big, &got);
    ok("the staged chunk opens once room exists", rc == VMESS_EOF && got == 19 + 9000 + 4);
    free(c);
}

static void test_tampering(void) {
    vless_dest_t d = domain_dest();
    static uint8_t wire[sizeof vmess_aes_response];
    static uint8_t plain[16384];
    size_t got = 0;
    uint8_t req[1024];
    size_t n = 0;

    vmess_client_t *c = make_client("aes-128-gcm", &d);
    if (!c) return;
    (void)vmess_client_build_request(c, NULL, 0, req, sizeof req, &n);
    memcpy(wire, vmess_aes_response, sizeof wire);
    wire[3] ^= 0x01;
    ok("a flipped bit in the header length is refused",
       vmess_client_feed_downstream(c, wire, sizeof wire, plain, sizeof plain, &got) ==
       VMESS_ERR_BAD_TAG);
    free(c);

    c = make_client("aes-128-gcm", &d);
    if (!c) return;
    memcpy(wire, vmess_aes_response, sizeof wire);
    /* 18 sealed length bytes, 4 + 16 sealed header bytes, 2 size bytes, then the
       first chunk's ciphertext */
    wire[18 + 20 + 2 + 3] ^= 0x80;
    int rc = vmess_client_feed_downstream(c, wire, sizeof wire, plain, sizeof plain, &got);
    ok("a flipped bit in a chunk is refused", rc == VMESS_ERR_BAD_TAG && got == 0);
    free(c);

    /* the response was sealed for the fixture's request keys; other keys mean
       a server answering someone else */
    c = make_client("aes-128-gcm", &d);
    if (!c) return;
    c->resp_v ^= 0xFF;
    rc = vmess_client_feed_downstream(c, vmess_aes_response, sizeof vmess_aes_response,
                                      plain, sizeof plain, &got);
    ok("a response header for another request is refused", rc == VMESS_ERR_PROTO);
    free(c);

    c = make_client("aes-128-gcm", &d);
    if (!c) return;
    uint8_t junk[64];
    memset(junk, 0x41, sizeof junk);
    rc = vmess_client_feed_downstream(c, junk, sizeof junk, plain, sizeof plain, &got);
    ok("a captive portal page is not a response", rc == VMESS_ERR_BAD_TAG);
    free(c);

    c = make_client("aes-128-gcm", &d);
    if (!c) return;
    static uint8_t flood[VMESS_STAGE_CAP + 1];
    rc = vmess_client_feed_downstream(c, flood, sizeof flood, plain, sizeof plain, &got);
    ok("input past the stage is refused", rc == VMESS_ERR_ARG);
    free(c);
}

static void test_chunk_limits(void) {
    vless_dest_t d = domain_dest();
    vmess_client_t *c = make_client("aes-128-gcm", &d);
    if (!c) return;
    static uint8_t data[VMESS_CHUNK_PAYLOAD_MAX + 1];
    static uint8_t out[VMESS_CHUNK_PAYLOAD_MAX + 2 + 16 + 64];
    size_t n = 0;
    ok("a chunk over the payload limit is refused",
       vmess_client_encrypt_chunk(c, data, sizeof data, out, sizeof out, &n) == VMESS_ERR_ARG);
    ok("a full chunk fits 8192 bytes",
       vmess_client_encrypt_chunk(c, data, VMESS_CHUNK_PAYLOAD_MAX, out, sizeof out, &n) ==
       VMESS_OK && n <= 8192);
    uint16_t before = c->enc_count;
    ok("an output that may not hold the padding is refused before any draw",
       vmess_client_encrypt_chunk(c, data, 10, out, 2 + 10 + 16, &n) == VMESS_ERR_ARG &&
       c->enc_count == before);
    free(c);

    /* a large first payload is split into chunks, never sealed whole */
    c = make_client("chacha20-poly1305", &d);
    if (!c) return;
    static uint8_t req[2 * 8192 + 512];
    static uint8_t body[VMESS_CHUNK_PAYLOAD_MAX + 100];
    ok("a payload over one chunk is split",
       vmess_client_build_request(c, body, sizeof body, req, sizeof req, &n) == VMESS_OK &&
       c->enc_count == 2);
    free(c);
}

/* the udp command: one chunk per datagram out, one length framed datagram per chunk back */
static void check_udp_fixture(const char *cipher, int64_t time_at,
                              const uint8_t *request, size_t request_len,
                              const uint8_t *response, size_t response_len) {
    vless_dest_t d = ipv4_dest();
    d.host_addr[0] = 127; d.host_addr[1] = 0; d.host_addr[2] = 0; d.host_addr[3] = 1;
    d.port = 19999;
    vmess_client_t *c = make_client(cipher, &d);
    ok("udp client", c != NULL);
    if (!c) return;
    c->packet = 1;
    c->unix_time = time_at;
    uint8_t wire[2048];
    size_t n = 0, m = 0;
    ok("udp request builds", vmess_client_build_request(c, (const uint8_t *)"ping", 4,
                                                       wire, sizeof wire, &n) == VMESS_OK);
    ok("udp second datagram is its own chunk",
       vmess_client_encrypt_chunk(c, (const uint8_t *)"pong!", 5, wire + n, sizeof wire - n, &m) == VMESS_OK);
    ok("udp request is the bytes xray answered", n + m == request_len && memcmp(wire, request, n + m) == 0);

    static const uint8_t want[] = { 0, 9, 'e', 'c', 'h', 'o', ':', 'p', 'i', 'n', 'g',
                                    0, 10, 'e', 'c', 'h', 'o', ':', 'p', 'o', 'n', 'g', '!' };
    uint8_t plain[256];
    size_t got = 0;
    int rc = drain(c, response, response_len, 5, plain, sizeof plain, &got);
    ok("udp response is read to its last chunk", rc == VMESS_NEED_MORE);
    ok("udp response comes back as framed datagrams", got == sizeof want && memcmp(plain, want, got) == 0);

    /* a datagram does not fit behind its frame when the output is short: the chunk stays staged */
    vmess_client_t *s = make_client(cipher, &d);
    ok("udp short client", s != NULL);
    if (s) {
        s->packet = 1;
        s->unix_time = time_at;
        ok("udp short request builds", vmess_client_build_request(s, (const uint8_t *)"ping", 4,
                                                                 wire, sizeof wire, &n) == VMESS_OK);
        size_t one = 0;
        rc = vmess_client_feed_downstream(s, response, response_len, plain, 10, &one);
        ok("udp chunk that misses its frame room stays staged", rc == VMESS_NEED_MORE && one == 0);
        rc = vmess_client_feed_downstream(s, NULL, 0, plain, sizeof plain, &one);
        ok("udp staged chunks open once there is room", rc == VMESS_OK && one == sizeof want);
        free(s);
    }
    free(c);
}

static void test_udp_fixtures(void) {
    check_udp_fixture("aes-128-gcm", vmess_udp_aes_time, vmess_udp_aes_request, sizeof vmess_udp_aes_request,
                      vmess_udp_aes_response, sizeof vmess_udp_aes_response);
    /* xray refuses a second request with the same session keys, so this one was recorded
       with the generator started at 100 */
    counter_start = 100;
    check_udp_fixture("chacha20-poly1305", vmess_udp_chacha_time, vmess_udp_chacha_request,
                      sizeof vmess_udp_chacha_request, vmess_udp_chacha_response,
                      sizeof vmess_udp_chacha_response);
    counter_start = 0;
}

static void test_names(void) {
    ok("aes by name", vmess_cipher_from_name("aes-128-gcm") == VMESS_CIPHER_AES_128_GCM);
    ok("chacha by name",
       vmess_cipher_from_name("chacha20-poly1305") == VMESS_CIPHER_CHACHA20_POLY1305);
    vmess_cipher_t a = vmess_cipher_from_name("auto");
    ok("auto picks a real cipher",
       a == VMESS_CIPHER_AES_128_GCM || a == VMESS_CIPHER_CHACHA20_POLY1305);
    ok("none runs as auto", vmess_cipher_from_name("none") == a);
    ok("a legacy cfb name runs as auto", vmess_cipher_from_name("aes-128-cfb") == a);
    ok("no name runs as auto", vmess_cipher_from_name(NULL) == a && vmess_cipher_from_name("") == a);
}

int main(void) {
    test_kdf();
    test_aes_fixture();
    test_chacha_fixture();
    test_small_output();
    test_tampering();
    test_chunk_limits();
    test_udp_fixtures();
    test_names();
    if (failed) return 1;
    puts("all vmess checks passed");
    return 0;
}
