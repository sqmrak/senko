/* hysteria2 stream and salamander framing against byte sequences written out
   by hand from the protocol description, plus the splits and caps a hostile
   server can use. the salamander vector comes from python's
   hashlib.blake2b(key + salt, digest_size=32) */
#include "hy2_proto.h"

#include <stdio.h>
#include <string.h>

static int failed;

#define CHECK(cond, label) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s (line %d)\n", label, __LINE__); failed = 1; } \
} while (0)

static void test_varint(void) {
    static const struct { uint64_t value; const char *hex; } table[] = {
        { 0, "00" }, { 37, "25" }, { 63, "3f" }, { 64, "4040" }, { 15293, "7bbd" },
        { 16383, "7fff" }, { 16384, "80004000" }, { 494878333, "9d7f3e7d" },
        { 1073741823, "bfffffff" }, { 1073741824, "c000000040000000" },
        { 151288809941952652ull, "c2197c5eff14e88c" },
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; ++i) {
        uint8_t buf[8];
        char hex[17];
        size_t n = hy2_varint_put(table[i].value, buf, sizeof buf);
        CHECK(n == strlen(table[i].hex) / 2, "varint size");
        for (size_t j = 0; j < n; ++j) sprintf(hex + j * 2, "%02x", buf[j]);
        CHECK(n && strcmp(hex, table[i].hex) == 0, "varint bytes");
        uint64_t back = 0;
        CHECK(hy2_varint_get(buf, n, &back) == n && back == table[i].value, "varint roundtrip");
        CHECK(n < 2 || hy2_varint_get(buf, n - 1, &back) == 0, "varint truncated");
    }
    uint8_t buf[8];
    CHECK(hy2_varint_put(1ull << 62, buf, sizeof buf) == 0, "varint over 62 bits");
    CHECK(hy2_varint_put(64, buf, 1) == 0, "varint cap");
    uint64_t v;
    CHECK(hy2_varint_get(buf, 0, &v) == 0, "varint empty");
}

static void dest_domain(vless_dest_t *d, const char *name, uint16_t port) {
    memset(d, 0, sizeof *d);
    d->atyp = VLESS_ADDR_DOMAIN;
    snprintf(d->domain, sizeof d->domain, "%s", name);
    d->port = port;
}

static void test_request(void) {
    vless_dest_t d;
    uint8_t out[512];
    size_t len = 0;

    dest_domain(&d, "example.com", 443);
    CHECK(hy2_tcp_request_build(&d, out, sizeof out, &len) == HY2_OK, "domain request");
    /* 0x401 as a two byte varint, 15 byte address, no padding */
    static const uint8_t want[] = { 0x44, 0x01, 0x0f, 'e','x','a','m','p','l','e','.','c','o','m',
                                    ':','4','4','3', 0x00 };
    CHECK(len == sizeof want && memcmp(out, want, len) == 0, "domain request bytes");

    memset(&d, 0, sizeof d);
    d.atyp = VLESS_ADDR_IPV4;
    d.host_addr[0] = 1; d.host_addr[1] = 1; d.host_addr[2] = 1; d.host_addr[3] = 1;
    d.port = 53;
    CHECK(hy2_tcp_request_build(&d, out, sizeof out, &len) == HY2_OK, "ipv4 request");
    CHECK(len == 2 + 1 + 10 + 1 && out[2] == 10 && memcmp(out + 3, "1.1.1.1:53", 10) == 0,
          "ipv4 request bytes");
    char text[64];
    CHECK(hy2_address_text(&d, text, sizeof text) == HY2_OK && strcmp(text, "1.1.1.1:53") == 0,
          "ipv4 text");

    memset(&d, 0, sizeof d);
    d.atyp = VLESS_ADDR_IPV6;
    d.host_addr[0] = 0x20; d.host_addr[1] = 0x01; d.host_addr[2] = 0x0d; d.host_addr[3] = 0xb8;
    d.host_addr[15] = 1;
    d.port = 8443;
    CHECK(hy2_address_text(&d, text, sizeof text) == HY2_OK && strcmp(text, "[2001:db8::1]:8443") == 0,
          "ipv6 text");

    dest_domain(&d, "example.com", 443);
    CHECK(hy2_tcp_request_build(&d, out, 5, &len) == HY2_ERR_ARG && len == 0, "request cap");
    dest_domain(&d, "", 443);
    CHECK(hy2_tcp_request_build(&d, out, sizeof out, &len) == HY2_ERR_ARG, "empty domain");
    d.atyp = (vless_atyp_t)9;
    CHECK(hy2_address_text(&d, text, sizeof text) == HY2_ERR_ARG, "unknown address type");
    dest_domain(&d, "example.com", 443);
    CHECK(hy2_address_text(&d, text, 8) == HY2_ERR_ARG, "address text cap");
}

static hy2_status_t feed_all(hy2_tcp_response_t *r, const uint8_t *in, size_t len, size_t *used) {
    return hy2_tcp_response_feed(r, in, len, used);
}

static void test_response(void) {
    hy2_tcp_response_t r;
    size_t used;

    /* status 0, no message, no padding, then application data */
    static const uint8_t ok_plain[] = { 0x00, 0x00, 0x00, 'H', 'I' };
    hy2_tcp_response_init(&r);
    CHECK(feed_all(&r, ok_plain, sizeof ok_plain, &used) == HY2_OK && used == 3, "plain ok");

    /* message "ok" and three padding bytes, followed by data */
    static const uint8_t ok_padded[] = { 0x00, 0x02, 'o', 'k', 0x03, 'x', 'y', 'z', 'D', 'A' };
    hy2_tcp_response_init(&r);
    CHECK(feed_all(&r, ok_padded, sizeof ok_padded, &used) == HY2_OK && used == 8, "padded ok");
    CHECK(strcmp(r.message, "ok") == 0, "ok message");

    /* every split point must give the same answer */
    for (size_t cut = 1; cut < 8; ++cut) {
        hy2_tcp_response_init(&r);
        size_t a = 0, b = 0;
        hy2_status_t first = feed_all(&r, ok_padded, cut, &a);
        CHECK(first == HY2_NEED_MORE && a == cut, "split first half");
        CHECK(feed_all(&r, ok_padded + cut, sizeof ok_padded - cut, &b) == HY2_OK && a + b == 8,
              "split second half");
    }
    /* one byte at a time, with a two byte length varint */
    {
        uint8_t big[2 + 1 + 64 + 1];
        size_t n = 0;
        big[n++] = 0x00;
        big[n++] = 0x40; big[n++] = 0x40; /* length 64 */
        memset(big + n, 'm', 64); n += 64;
        big[n++] = 0x00;
        hy2_tcp_response_init(&r);
        hy2_status_t st = HY2_NEED_MORE;
        size_t total = 0;
        for (size_t i = 0; i < n && st == HY2_NEED_MORE; ++i) {
            size_t one = 0;
            st = feed_all(&r, big + i, 1, &one);
            total += one;
        }
        CHECK(st == HY2_OK && total == n && r.message_len == 64, "byte at a time");
    }

    /* the server refuses the destination */
    static const uint8_t refused[] = { 0x01, 0x04, 'd', 'e', 'n', 'y', 0x00 };
    hy2_tcp_response_init(&r);
    CHECK(feed_all(&r, refused, sizeof refused, &used) == HY2_REJECTED && used == sizeof refused,
          "rejected");
    CHECK(r.status == 1 && strcmp(r.message, "deny") == 0, "rejected message");

    /* a length over the cap is refused before anything is stored */
    static const uint8_t huge_message[] = { 0x00, 0x48, 0x01 }; /* 2049 */
    hy2_tcp_response_init(&r);
    CHECK(feed_all(&r, huge_message, sizeof huge_message, &used) == HY2_ERR_PROTO, "message cap");
    static const uint8_t huge_padding[] = { 0x00, 0x00, 0x50, 0x01 }; /* 4097 */
    hy2_tcp_response_init(&r);
    CHECK(feed_all(&r, huge_padding, sizeof huge_padding, &used) == HY2_ERR_PROTO, "padding cap");
    static const uint8_t max_message_len[] = { 0x00, 0x48, 0x00, 'a' }; /* 2048, one byte so far */
    hy2_tcp_response_init(&r);
    CHECK(feed_all(&r, max_message_len, sizeof max_message_len, &used) == HY2_NEED_MORE &&
          used == sizeof max_message_len, "message at the cap");

    /* nothing at all, and a header cut inside every field */
    hy2_tcp_response_init(&r);
    CHECK(feed_all(&r, ok_plain, 0, &used) == HY2_NEED_MORE && used == 0, "empty input");
    CHECK(hy2_tcp_response_feed(NULL, ok_plain, 1, &used) == HY2_ERR_ARG, "null response");
    CHECK(hy2_tcp_response_feed(&r, NULL, 1, &used) == HY2_ERR_ARG, "null input");
}

static void test_salamander(void) {
    static const uint8_t key[] = "cry_me_a_r1ver";
    static const uint8_t salt[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t plain[50];
    for (size_t i = 0; i < sizeof plain; ++i) plain[i] = (uint8_t)i;
    static const uint8_t want[] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x64,0xa0,0x89,0xeb,0xd3,0x95,0xf8,0x19,
        0x10,0x63,0x58,0xb3,0x1b,0xcc,0x52,0x52,0xca,0x85,0x64,0x91,0x19,0x0a,0x94,0x8c,
        0x9d,0x86,0x61,0x24,0x87,0xe5,0x17,0xf6,0x44,0x80,0xa9,0xcb,0xf3,0xb5,0xd8,0x39,
        0x30,0x43,0x78,0x93,0x3b,0xec,0x72,0x72,0xea,0xa5
    };
    uint8_t wire[128], back[128];
    size_t wire_len = 0, back_len = 0;

    CHECK(hy2_salamander_wrap(key, sizeof key - 1, salt, plain, sizeof plain, wire, sizeof wire,
                              &wire_len) == HY2_OK, "wrap");
    CHECK(wire_len == sizeof want && memcmp(wire, want, wire_len) == 0, "wrap vector");
    CHECK(hy2_salamander_unwrap(key, sizeof key - 1, wire, wire_len, back, sizeof back,
                                &back_len) == HY2_OK, "unwrap");
    CHECK(back_len == sizeof plain && memcmp(back, plain, back_len) == 0, "unwrap vector");

    /* a packet longer than the 32 byte digest repeats it, so check a full mtu */
    uint8_t big[1200], big_wire[1208], big_back[1200];
    for (size_t i = 0; i < sizeof big; ++i) big[i] = (uint8_t)(i * 7);
    CHECK(hy2_salamander_wrap(key, sizeof key - 1, salt, big, sizeof big, big_wire, sizeof big_wire,
                              &wire_len) == HY2_OK && wire_len == 1208, "wrap mtu");
    CHECK(hy2_salamander_unwrap(key, sizeof key - 1, big_wire, wire_len, big_back, sizeof big_back,
                                &back_len) == HY2_OK && back_len == 1200 &&
          memcmp(big, big_back, 1200) == 0, "unwrap mtu");

    /* the wrong key gives different bytes, not an error: there is no tag */
    static const uint8_t other[] = "another";
    CHECK(hy2_salamander_unwrap(other, sizeof other - 1, wire, 58, back, sizeof back, &back_len) ==
          HY2_OK && memcmp(back, plain, 50) != 0, "wrong key");

    CHECK(hy2_salamander_wrap(key, sizeof key - 1, salt, plain, sizeof plain, wire, 57, &wire_len) ==
          HY2_ERR_ARG && wire_len == 0, "wrap cap");
    CHECK(hy2_salamander_unwrap(key, sizeof key - 1, wire, 7, back, sizeof back, &back_len) ==
          HY2_ERR_ARG, "unwrap shorter than the salt");
    CHECK(hy2_salamander_unwrap(key, sizeof key - 1, want, sizeof want, back, 49, &back_len) ==
          HY2_ERR_ARG, "unwrap cap");
    CHECK(hy2_salamander_unwrap(key, sizeof key - 1, want, 8, back, sizeof back, &back_len) ==
          HY2_OK && back_len == 0, "unwrap salt only");
    CHECK(hy2_salamander_wrap(NULL, 0, salt, plain, sizeof plain, wire, sizeof wire, &wire_len) ==
          HY2_ERR_ARG, "wrap without a key");
    uint8_t long_key[HY2_SALAMANDER_KEY_MAX + 1];
    memset(long_key, 'k', sizeof long_key);
    CHECK(hy2_salamander_wrap(long_key, sizeof long_key, salt, plain, sizeof plain, wire, sizeof wire,
                              &wire_len) == HY2_ERR_ARG, "wrap key over the cap");
    CHECK(hy2_salamander_wrap(long_key, HY2_SALAMANDER_KEY_MAX, salt, plain, sizeof plain, wire,
                              sizeof wire, &wire_len) == HY2_OK, "wrap key at the cap");
}

int main(void) {
    test_varint();
    test_request();
    test_response();
    test_salamander();
    if (failed) return 1;
    puts("hy2 proto ok");
    return 0;
}
