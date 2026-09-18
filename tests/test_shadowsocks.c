#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "shadowsocks_client.h"
#include "config.h"
#include "profiles.h"
#include "b64.h"

static void test_ss_ciphers_roundtrip(const char *cipher) {
    vless_dest_t dest;
    memset(&dest, 0, sizeof(dest));
    dest.atyp = VLESS_ADDR_DOMAIN;
    dest.port = 80;
    strncpy(dest.domain, "example.com", sizeof(dest.domain) - 1);

    shadowsocks_client_t sender, receiver;
    memset(&sender, 0, sizeof(sender));
    memset(&receiver, 0, sizeof(receiver));
    ss_client_init(&sender, cipher, "test_password_123", &dest);
    ss_client_init(&receiver, cipher, "test_password_123", NULL);

    /* 1. Sender builds request to target */
    uint8_t req[1024];
    size_t req_len = 0;
    const char *initial_data = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    assert(ss_client_build_request(&sender, (const uint8_t *)initial_data, strlen(initial_data),
                                   req, sizeof(req), &req_len) == 0);
    assert(req_len > sender.salt_len);

    /* Feed entire request (salt + request chunk) to receiver */
    uint8_t out[1024];
    size_t written = 0;
    int r = ss_client_feed_downstream(&receiver, req, req_len, out, sizeof(out), &written);
    assert(r == SS_OK);
    assert(written > strlen(initial_data));

    /* First part of decrypted stream should be SOCKS address header:
       [0] atyp (3)
       [1] domain len (11)
       [2..12] "example.com"
       [13..14] port (80) */
    assert(out[0] == 3);
    assert(out[1] == 11);
    assert(memcmp(out + 2, "example.com", 11) == 0);
    assert(out[13] == 0x00 && out[14] == 0x50);
    /* Followed by initial data */
    assert(memcmp(out + 15, initial_data, strlen(initial_data)) == 0);

    /* 2. Test subsequent chunk encryption / decryption roundtrip */
    uint8_t chunk_buf[512];
    size_t chunk_len = 0;
    const char *msg = "Hello Shadowsocks!";
    assert(ss_client_encrypt_chunk(&sender, (const uint8_t *)msg, strlen(msg),
                                  chunk_buf, sizeof(chunk_buf), &chunk_len) == 0);

    written = 0;
    r = ss_client_feed_downstream(&receiver, chunk_buf, chunk_len, out, sizeof(out), &written);
    assert(r == SS_OK);
    assert(written == strlen(msg));
    assert(memcmp(out, msg, strlen(msg)) == 0);

    printf("ok test_ss_roundtrip for %s\n", cipher);
}

/* a single wire read can carry more full chunks than the caller's plain
   buffer holds; the leftover chunk must stay staged rather than desync
   the receive nonce or drop already-decoded chunks */
static void test_ss_plain_cap_split(void) {
    vless_dest_t dest;
    memset(&dest, 0, sizeof(dest));
    dest.atyp = VLESS_ADDR_DOMAIN;
    dest.port = 80;
    strncpy(dest.domain, "example.com", sizeof(dest.domain) - 1);

    shadowsocks_client_t sender, receiver;
    memset(&sender, 0, sizeof(sender));
    memset(&receiver, 0, sizeof(receiver));
    ss_client_init(&sender, "aes-256-gcm", "test_password_123", &dest);
    ss_client_init(&receiver, "aes-256-gcm", "test_password_123", NULL);

    uint8_t req[256];
    size_t req_len = 0;
    assert(ss_client_build_request(&sender, NULL, 0, req, sizeof(req), &req_len) == 0);
    uint8_t addr_out[256];
    size_t addr_len = 0;
    assert(ss_client_feed_downstream(&receiver, req, req_len, addr_out, sizeof(addr_out), &addr_len) == SS_OK);

    const char *msg_a = "first chunk payload";
    const char *msg_b = "second chunk payload";
    uint8_t chunk_a[64], chunk_b[64];
    size_t alen = 0, blen = 0;
    assert(ss_client_encrypt_chunk(&sender, (const uint8_t *)msg_a, strlen(msg_a), chunk_a, sizeof(chunk_a), &alen) == 0);
    assert(ss_client_encrypt_chunk(&sender, (const uint8_t *)msg_b, strlen(msg_b), chunk_b, sizeof(chunk_b), &blen) == 0);

    uint8_t wire[256];
    memcpy(wire, chunk_a, alen);
    memcpy(wire + alen, chunk_b, blen);

    /* plain_cap fits chunk_a exactly, so chunk_b cannot be decoded this call */
    uint8_t small_out[32];
    size_t got = 0;
    int r = ss_client_feed_downstream(&receiver, wire, alen + blen, small_out, strlen(msg_a), &got);
    assert(r == SS_OK);
    assert(got == strlen(msg_a));
    assert(memcmp(small_out, msg_a, strlen(msg_a)) == 0);

    /* chunk_b must still be staged and decode correctly on the next call */
    uint8_t out2[64];
    size_t got2 = 0;
    r = ss_client_feed_downstream(&receiver, NULL, 0, out2, sizeof(out2), &got2);
    assert(r == SS_OK);
    assert(got2 == strlen(msg_b));
    assert(memcmp(out2, msg_b, strlen(msg_b)) == 0);

    printf("ok test_ss_plain_cap_split\n");
}

/* the aead chunk length prefix is 14 bits, so the request's target-address
   header plus its initial payload can never exceed 0x3fff bytes; the request
   builder must reject anything larger itself rather than let a too-large
   chunk fail one call later inside the encryptor */
static void test_ss_build_request_chunk_boundary(void) {
    vless_dest_t dest;
    memset(&dest, 0, sizeof(dest));
    dest.atyp = VLESS_ADDR_DOMAIN;
    dest.port = 80;
    strncpy(dest.domain, "example.com", sizeof(dest.domain) - 1);
    /* atyp(1) + len(1) + "example.com"(11) + port(2) = 15 bytes of header */
    const size_t header_len = 15;

    shadowsocks_client_t sender;
    memset(&sender, 0, sizeof(sender));
    ss_client_init(&sender, "aes-256-gcm", "test_password_123", &dest);

    static uint8_t payload[0x4000];
    memset(payload, 'A', sizeof payload);

    uint8_t req[0x4100];
    size_t req_len = 0;
    assert(ss_client_build_request(&sender, payload, 0x3fff - header_len,
                                   req, sizeof(req), &req_len) == 0);

    memset(&sender, 0, sizeof(sender));
    ss_client_init(&sender, "aes-256-gcm", "test_password_123", &dest);
    assert(ss_client_build_request(&sender, payload, 0x3fff - header_len + 1,
                                   req, sizeof(req), &req_len) == SS_ERR_ARG);

    printf("ok test_ss_build_request_chunk_boundary\n");
}

static void test_ss_uri_parse(void) {
    vl_server_t sv;
    memset(&sv, 0, sizeof(sv));

    /* Test SIP002 format: ss://BASE64(method:password@host:port)#tag */
    const char *raw_cred = "aes-256-gcm:mysecretpass@ss.example.com:8388";
    char b64[256];
    size_t b64_len = 0;
    assert(b64_encode((const uint8_t *)raw_cred, strlen(raw_cred), b64, sizeof(b64), &b64_len) == 0);

    char link[512];
    snprintf(link, sizeof(link), "ss://%s#MyNode", b64);

    assert(cfg_parse_link(link, &sv) == 0);
    assert(sv.proto == VL_PROTO_SHADOWSOCKS);
    assert(strcmp(sv.encryption, "aes-256-gcm") == 0);
    assert(strcmp(sv.pass, "mysecretpass") == 0);
    assert(strcmp(sv.host, "ss.example.com") == 0);
    assert(sv.port == 8388);
    assert(strcmp(sv.remark, "MyNode") == 0);

    /* Validation */
    char err[64];
    assert(cfg_validate_server(&sv, err, sizeof(err)) == 1);

    /* Test alternate format: ss://BASE64(method:password)@host:port#tag */
    memset(&sv, 0, sizeof(sv));
    const char *userpass = "chacha20-ietf-poly1305:p@ss:word";
    assert(b64_encode((const uint8_t *)userpass, strlen(userpass), b64, sizeof(b64), &b64_len) == 0);
    snprintf(link, sizeof(link), "ss://%s@1.2.3.4:9000#AltSS", b64);

    assert(cfg_parse_link(link, &sv) == 0);
    assert(sv.proto == VL_PROTO_SHADOWSOCKS);
    assert(strcmp(sv.encryption, "chacha20-ietf-poly1305") == 0);
    assert(strcmp(sv.pass, "p@ss:word") == 0);
    assert(strcmp(sv.host, "1.2.3.4") == 0);
    assert(sv.port == 9000);
    assert(strcmp(sv.remark, "AltSS") == 0);
    assert(cfg_validate_server(&sv, err, sizeof(err)) == 1);

    printf("ok test_ss_uri_parse\n");
}

/* the legacy ss:// form with no '@' base64-decodes its whole body and
   reparses the result, so a subscription panel (untrusted content) can chain
   "ss://base64(ss://base64(...))" indefinitely; cfg_parse_link must bound
   that self-recursion instead of growing the C stack with the chain */
static void test_ss_legacy_chain_bounded(void) {
    char link[4096];
    snprintf(link, sizeof link, "ss://not-a-real-target-no-at-sign");

    for (int i = 0; i < 15; ++i) {
        char b64[4096];
        size_t b64_len = 0;
        assert(b64_encode((const uint8_t *)link, strlen(link), b64, sizeof b64, &b64_len) == 0);
        int n = snprintf(link, sizeof link, "ss://%s", b64);
        assert(n > 0 && (size_t)n < (int)sizeof link);
    }

    vl_server_t sv;
    memset(&sv, 0, sizeof(sv));
    assert(cfg_parse_link(link, &sv) != CFG_OK);

    printf("ok test_ss_legacy_chain_bounded\n");
}

static void test_ss_clash_parse(void) {
    const char *clash_yaml =
        "proxies:\n"
        "  - name: \"SS Node\"\n"
        "    type: ss\n"
        "    server: ss.domain.com\n"
        "    port: 8388\n"
        "    cipher: aes-128-gcm\n"
        "    password: mypass\n";

    vl_server_t out[4];
    size_t n = profiles_parse_clash(clash_yaml, strlen(clash_yaml), out, 4);
    assert(n == 1);
    assert(out[0].proto == VL_PROTO_SHADOWSOCKS);
    assert(strcmp(out[0].remark, "SS Node") == 0);
    assert(strcmp(out[0].host, "ss.domain.com") == 0);
    assert(out[0].port == 8388);
    assert(strcmp(out[0].encryption, "aes-128-gcm") == 0);
    assert(strcmp(out[0].pass, "mypass") == 0);

    printf("ok test_ss_clash_parse\n");
}

static void test_published_ss_link(void) {
    const char *link =
        "ss://Y2hhY2hhMjAtaWV0Zi1wb2x5MTMwNTp6akF4Wg@83.171.225.55:1443"
        "#Published";
    vl_server_t sv;
    char err[64];

    assert(cfg_parse_link(link, &sv) == CFG_OK);
    assert(sv.proto == VL_PROTO_SHADOWSOCKS);
    assert(strcmp(sv.encryption, "chacha20-ietf-poly1305") == 0);
    assert(strcmp(sv.pass, "zjAxZ") == 0);
    assert(strcmp(sv.host, "83.171.225.55") == 0);
    assert(sv.port == 1443);
    assert(cfg_validate_server(&sv, err, sizeof(err)) == 1);

    printf("ok test_published_ss_link\n");
}

int main(void) {
    test_ss_ciphers_roundtrip("aes-256-gcm");
    test_ss_ciphers_roundtrip("aes-128-gcm");
    test_ss_ciphers_roundtrip("chacha20-ietf-poly1305");
    test_ss_plain_cap_split();
    test_ss_build_request_chunk_boundary();
    test_ss_uri_parse();
    test_ss_legacy_chain_bounded();
    test_ss_clash_parse();
    test_published_ss_link();
    printf("all shadowsocks tests passed!\n");
    return 0;
}
