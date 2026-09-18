#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <arpa/inet.h>
#include "trojan_client.h"
#include "config.h"
#include "profiles.h"

static void test_trojan_hash(void) {
    char hex[TROJAN_HEX_LEN + 1];
    int r = trojan_hash_password("password", hex);
    assert(r == 0);
    assert(strlen(hex) == 56);
    /* known sha224 of "password" */
    assert(strcmp(hex, "d63dc919e201d7bc4c825630d2cf25fdc93d4b2f0d46706d29038d01") == 0);
    printf("ok test_trojan_hash\n");
}

static void test_trojan_request(void) {
    trojan_client_t tc;
    vless_dest_t dest;
    memset(&dest, 0, sizeof(dest));
    dest.atyp = VLESS_ADDR_IPV4;
    dest.port = 443;
    inet_pton(AF_INET, "1.2.3.4", dest.host_addr);

    trojan_client_init(&tc, "mysecret", &dest);

    /* IPv4 target request */
    uint8_t buf[256];
    size_t out_len = 0;
    assert(trojan_client_build_request(&tc, NULL, 0, buf, sizeof(buf), &out_len) == 0);
    /* 56 (hash) + 2 (\r\n) + 1 (cmd) + 1 (atyp) + 4 (ip) + 2 (port) + 2 (\r\n) = 68 */
    assert(out_len == 68);
    assert(memcmp(buf, tc.hex_hash, 56) == 0);
    assert(buf[56] == '\r' && buf[57] == '\n');
    assert(buf[58] == 1); /* CONNECT */
    assert(buf[59] == 1); /* ATYP_IPV4 */
    assert(buf[60] == 1 && buf[61] == 2 && buf[62] == 3 && buf[63] == 4);
    assert(buf[64] == 0x01 && buf[65] == 0xbb); /* port 443 */
    assert(buf[66] == '\r' && buf[67] == '\n');

    /* Domain target */
    memset(&dest, 0, sizeof(dest));
    dest.atyp = VLESS_ADDR_DOMAIN;
    strncpy(dest.domain, "example.com", sizeof(dest.domain) - 1);
    dest.port = 80;

    trojan_client_init(&tc, "mysecret", &dest);
    assert(trojan_client_build_request(&tc, NULL, 0, buf, sizeof(buf), &out_len) == 0);
    /* 56 + 2 + 1 + 1 (atyp=3) + 1 (len=11) + 11 + 2 + 2 = 76 */
    assert(out_len == 76);
    assert(buf[59] == 3); /* ATYP_DOMAIN */
    assert(buf[60] == 11);
    assert(memcmp(buf + 61, "example.com", 11) == 0);
    assert(buf[72] == 0x00 && buf[73] == 0x50); /* port 80 */
    assert(buf[74] == '\r' && buf[75] == '\n');

    printf("ok test_trojan_request\n");
}

static void test_trojan_uri_parse(void) {
    vl_server_t sv;
    memset(&sv, 0, sizeof(sv));

    const char *link = "trojan://pass123@trojan.example.com:443?security=tls&type=ws&path=%2Ftrojan-ws&sni=sni.example.com#MyTrojan";
    assert(cfg_parse_link(link, &sv) == 0);
    assert(sv.proto == VL_PROTO_TROJAN);
    assert(strcmp(sv.pass, "pass123") == 0);
    assert(strcmp(sv.host, "trojan.example.com") == 0);
    assert(sv.port == 443);
    assert(sv.net == VL_NET_WS);
    assert(sv.security == VL_SEC_TLS);
    assert(strcmp(sv.path, "/trojan-ws") == 0);
    assert(strcmp(sv.sni, "sni.example.com") == 0);
    assert(strcmp(sv.remark, "MyTrojan") == 0);
    assert(sv.insecure == 0);

    /* Validation */
    char err[64];
    assert(cfg_validate_server(&sv, err, sizeof(err)) == 1);

    printf("ok test_trojan_uri_parse\n");
}

/* many trojan nodes sit behind a bare ip or a self-signed cert and rely on
   the client honoring allowInsecure/insecure instead of presenting a real
   chain; a client that silently drops this parameter enforces strict
   verification against every one of them and fails the tls handshake */
static void test_trojan_insecure_parse(void) {
    vl_server_t sv;

    memset(&sv, 0, sizeof(sv));
    assert(cfg_parse_link(
        "trojan://pass@1.2.3.4:443?security=tls&allowInsecure=1#t", &sv) == 0);
    assert(sv.insecure == 1);

    memset(&sv, 0, sizeof(sv));
    assert(cfg_parse_link(
        "trojan://pass@1.2.3.4:443?security=tls&insecure=1#t", &sv) == 0);
    assert(sv.insecure == 1);

    memset(&sv, 0, sizeof(sv));
    assert(cfg_parse_link(
        "trojan://pass@1.2.3.4:443?security=tls&allowInsecure=0#t", &sv) == 0);
    assert(sv.insecure == 0);

    printf("ok test_trojan_insecure_parse\n");
}

static void test_trojan_clash_parse(void) {
    const char *clash_yaml =
        "proxies:\n"
        "  - name: \"Trojan Node\"\n"
        "    type: trojan\n"
        "    server: tr.domain.com\n"
        "    port: 443\n"
        "    password: p123\n"
        "    sni: tr.domain.com\n"
        "    network: ws\n"
        "    ws-opts:\n"
        "      path: /ws\n";

    vl_server_t out[4];
    size_t n = profiles_parse_clash(clash_yaml, strlen(clash_yaml), out, 4);
    assert(n == 1);
    assert(out[0].proto == VL_PROTO_TROJAN);
    assert(strcmp(out[0].remark, "Trojan Node") == 0);
    assert(strcmp(out[0].host, "tr.domain.com") == 0);
    assert(out[0].port == 443);
    assert(strcmp(out[0].pass, "p123") == 0);
    assert(out[0].net == VL_NET_WS);
    assert(strcmp(out[0].path, "/ws") == 0);

    printf("ok test_trojan_clash_parse\n");
}

static void test_published_trojan_links(void) {
    const char *tls_ws =
        "trojan://trojan@104.18.12.229:8443?security=tls&allowInsecure=1"
        "&sni=longwanghen.pages.dev&type=ws&host=&path=%2F#Published";
    const char *plain =
        "trojan://kiu30181q5ftupbz@185.204.168.28:1819?type=tcp&security=none"
        "#Published";
    vl_server_t sv;
    char err[64];

    assert(cfg_parse_link(tls_ws, &sv) == CFG_OK);
    assert(sv.proto == VL_PROTO_TROJAN);
    assert(sv.net == VL_NET_WS);
    assert(sv.security == VL_SEC_TLS);
    assert(strcmp(sv.sni, "longwanghen.pages.dev") == 0);
    assert(cfg_validate_server(&sv, err, sizeof(err)) == 1);

    assert(cfg_parse_link(plain, &sv) == CFG_OK);
    assert(cfg_validate_server(&sv, err, sizeof(err)) == 0);
    assert(strcmp(err, "trojan requires tls") == 0);

    printf("ok test_published_trojan_links\n");
}

int main(void) {
    test_trojan_hash();
    test_trojan_request();
    test_trojan_uri_parse();
    test_trojan_insecure_parse();
    test_trojan_clash_parse();
    test_published_trojan_links();
    printf("all trojan tests passed!\n");
    return 0;
}
