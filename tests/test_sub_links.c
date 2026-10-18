/* share links as panels and exporters actually write them. every key and id
   here is synthetic */
#include "config.h"
#include "vless.h"
#include "store.h"
#include "b64.h"

#include <stdlib.h>

#include <stdio.h>
#include <string.h>

static int g_fail;
static void ok(const char *what, int cond) {
    if (cond) return;
    g_fail++;
    fprintf(stderr, "FAIL %s\n", what);
}

#define UUID "11111111-1111-4111-8111-111111111111"
#define PBK  "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"

static vl_server_t s;
static char why[160];

/* parses and validates; 1 when the server is usable */
static int usable(const char *link) {
    why[0] = '\0';
    memset(&s, 0, sizeof s);
    return cfg_parse_link_ex(link, &s, why, sizeof why) == CFG_OK &&
           cfg_validate_server(&s, why, sizeof why);
}

/* 1 when the link is refused and the reason names what */
static int refused_for(const char *link, const char *what) {
    return !usable(link) && strstr(why, what) != NULL;
}

static void test_vless(void) {
    ok("scheme case does not matter",
       usable("VLESS://" UUID "@example.com:443?security=reality&pbk=" PBK "&sid=ab&type=tcp#n"));
    ok("uuid without dashes",
       usable("vless://11111111111141118111111111111111@example.com:443?security=tls&type=tcp#n") &&
       strcmp(s.uuid, UUID) == 0);
    ok("xray string id",
       usable("vless://my-user-7@example.com:443?security=tls&type=ws&path=%2F#n") &&
       strcmp(s.uuid, "my-user-7") == 0);
    uint8_t raw[16];
    /* sha1(16 zero bytes || "my-user-7")[:16] with version 5 and the rfc variant */
    ok("string id maps like xray",
       vless_uuid_parse("my-user-7", raw) == VLESS_OK && (raw[6] >> 4) == 5 && (raw[8] >> 6) == 2);
    ok("31 byte ids are refused like xray",
       refused_for("vless://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa@example.com:443?security=tls#n", "id"));
    ok("security=false is plain",
       usable("vless://" UUID "@example.com:80?security=false&type=ws&path=%2F#n") &&
       s.security == VL_SEC_NONE);
    ok("vision udp443 flow is vision",
       usable("vless://" UUID "@example.com:443?security=reality&pbk=" PBK "&type=tcp&flow=xtls-rprx-vision-udp443#n") &&
       strcmp(s.flow, "xtls-rprx-vision") == 0);
    ok("html escaped query",
       usable("vless://" UUID "@example.com:443?security=tls&amp;type=ws&amp;path=%2Fa#n") &&
       s.net == VL_NET_WS && strcmp(s.path, "/a") == 0);
    ok("surrounding junk is trimmed",
       usable("\xEF\xBB\xBF  vless://" UUID "@example.com:443?security=tls&type=tcp#n \t"));
    ok("websocket spelled out",
       usable("vless://" UUID "@example.com:443?security=tls&type=websocket&path=%2F#n") &&
       s.net == VL_NET_WS);
    ok("gun is grpc",
       usable("vless://" UUID "@example.com:443?security=tls&type=gun&serviceName=svc#n") &&
       s.net == VL_NET_GRPC);
    ok("reality key aliases",
       usable("vless://" UUID "@example.com:443?security=reality&publicKey=" PBK "&shortId=ab&type=tcp#n") &&
       strcmp(s.pbk, PBK) == 0 && strcmp(s.sid, "ab") == 0);
    ok("httpupgrade is named",
       refused_for("vless://" UUID "@example.com:80?security=none&type=httpupgrade&path=%2F#n",
                   "httpupgrade"));
    ok("tcp with an http header is named",
       refused_for("vless://" UUID "@example.com:80?security=none&type=tcp&headerType=http#n",
                   "http header"));
    ok("vless encryption is named",
       refused_for("vless://" UUID "@example.com:443?security=tls&encryption=mlkem768x25519plus.native.0rtt.x#n",
                   "encryption"));
    ok("kcp is named",
       refused_for("vless://" UUID "@example.com:443?security=none&type=kcp#n", "kcp"));
    ok("no port is refused", !usable("vless://" UUID "@example.com?security=tls#n"));
    ok("an at sign in the query is not the userinfo",
       usable("hy2://auth@example.com:35000/?sni=edge.example&obfs=salamander"
              "&obfs-password=@secret-word&note=@channel#n") &&
       strcmp(s.host, "example.com") == 0 && strcmp(s.pass, "auth") == 0 &&
       strcmp(s.obfs_password, "@secret-word") == 0);
    ok("an empty vless id is refused",
       !usable("vless://@example.com:443?security=tls&sni=@channel#n"));
    ok("remark is decoded",
       usable("vless://" UUID "@example.com:443?security=tls#%F0%9F%87%A9%F0%9F%87%AA%20DE") &&
       strcmp(s.remark, "\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA DE") == 0);
}

static void test_trojan(void) {
    ok("password with an at sign",
       usable("trojan://pa@ss@example.com:443?security=tls&sni=example.com#n") &&
       strcmp(s.pass, "pa@ss") == 0 && strcmp(s.host, "example.com") == 0);
    char link[512];
    char pass[161];
    memset(pass, 'p', 160);
    pass[160] = '\0';
    snprintf(link, sizeof link, "trojan://%s@example.com:443?security=tls#n", pass);
    ok("long password", usable(link) && strcmp(s.pass, pass) == 0);
    ok("a raw hash in the password",
       usable("trojan://ab#cd@example.com:443?security=tls#name") &&
       strcmp(s.pass, "ab#cd") == 0 && strcmp(s.remark, "name") == 0);
    ok("peer is sni",
       usable("trojan://pw@1.2.3.4:443?peer=edge.example&allowInsecure=1#n") &&
       strcmp(s.sni, "edge.example") == 0 && s.insecure);
    ok("trojan over reality is named",
       refused_for("trojan://pw@example.com:443?security=reality&pbk=" PBK "#n", "reality"));
}

static void test_shadowsocks(void) {
    ok("sip002 url safe base64 without padding",
       usable("ss://YWVzLTI1Ni1nY206cGFzcw@example.com:8388#n") &&
       strcmp(s.encryption, "aes-256-gcm") == 0 && strcmp(s.pass, "pass") == 0);
    ok("sip002 plain userinfo",
       usable("ss://chacha20-ietf-poly1305:pa%40ss@example.com:8388#n") &&
       strcmp(s.pass, "pa@ss") == 0);
    /* base64("aes-128-gcm:secret@example.com:443") */
    ok("legacy whole base64",
       usable("ss://YWVzLTEyOC1nY206c2VjcmV0QGV4YW1wbGUuY29tOjQ0Mw==#legacy") &&
       strcmp(s.host, "example.com") == 0 && s.port == 443 &&
       strcmp(s.encryption, "aes-128-gcm") == 0 && strcmp(s.remark, "legacy") == 0);
    ok("method case and old name",
       usable("ss://AES-128-GCM:x@example.com:1#n") && strcmp(s.encryption, "aes-128-gcm") == 0 &&
       usable("ss://chacha20-poly1305:x@example.com:1#n") &&
       strcmp(s.encryption, "chacha20-ietf-poly1305") == 0);
    ok("plugin is named",
       refused_for("ss://YWVzLTI1Ni1nY206cGFzcw@example.com:8388/?plugin=obfs-local%3Bobfs%3Dhttp#n",
                   "plugin"));
    ok("2022 cipher is named",
       refused_for("ss://2022-blake3-aes-128-gcm:a2V5@example.com:8388#n", "2022"));
    ok("sip002 with a query and no plugin",
       usable("ss://YWVzLTI1Ni1nY206cGFzcw@example.com:8388?type=tcp#n"));
}

static void test_socks_and_hysteria(void) {
    /* v2rayn writes the socks userinfo as base64("user:pass") */
    ok("socks with base64 userinfo",
       usable("socks://dXNlcjpwYXNz@example.com:1080#n") && s.proto == VL_PROTO_SOCKS5 &&
       strcmp(s.user, "user") == 0 && strcmp(s.pass, "pass") == 0);
    ok("socks5 plain",
       usable("socks5://u:p@example.com:1080") && strcmp(s.user, "u") == 0);
    ok("hy2 with obfs",
       usable("hy2://auth@example.com:443?sni=edge.example&obfs=salamander&obfs-password=y&insecure=1#n") &&
       strcmp(s.obfs, "salamander") == 0 && s.insecure && strcmp(s.sni, "edge.example") == 0);
    ok("hysteria2 user and password stay one token",
       usable("hysteria2://user:pass@example.com:443#n") && strcmp(s.pass, "user:pass") == 0);
    ok("hysteria2 port hopping",
       usable("hysteria2://a@example.com:443,5000-6000?sni=example.com#n") && s.port == 443);
}

static void test_unsupported_schemes(void) {
    ok("tuic is named", refused_for("tuic://u:p@example.com:443#n", "tuic"));
    ok("ssr is named", refused_for("ssr://ZXhhbXBsZQ", "ssr"));
    ok("wireguard is named", refused_for("wireguard://k@example.com:51820#n", "wireguard"));
    ok("hysteria v1 is named", refused_for("hysteria://example.com:443?auth=x#n", "hysteria"));
    ok("unknown text is not a link", !usable("just some words"));
}

static void test_subscription(void) {
    static vl_server_t out[16];
    size_t count = 0;
    cfg_import_stats_t stats;
    const char body[] =
        "\xEF\xBB\xBF# profile-title: test\r\n"
        "vless://" UUID "@example.com:443?security=tls&type=tcp#one\r\n"
        "ssr://ZXhhbXBsZQ\r\n"
        "ssr://ZXhhbXBsZQ\r\n"
        "tuic://u:p@example.com:443#t\r\n"
        "trojan://pw@example.com:443?security=tls#two\r\n";
    ok("mixed body parses",
       cfg_parse_subscription_ex(body, sizeof body - 1, out, 16, &count, &stats) == CFG_OK &&
       count == 2 && stats.skipped == 3);
    ok("skips are grouped by what",
       strcmp(stats.kinds[0].what, "ssr") == 0 && stats.kinds[0].count == 2 &&
       strcmp(stats.kinds[1].what, "tuic") == 0 && stats.kinds[1].count == 1);
    char text[160];
    cfg_import_stats_text(&stats, text, sizeof text);
    ok("the summary names them", strcmp(text, "skipped 3: ssr 2, tuic 1") == 0);

    static char longline[12000];
    memset(longline, 'a', sizeof longline - 1);
    memcpy(longline, "vless://", 8);
    longline[sizeof longline - 2] = '\n';
    ok("an overlong line is counted, not dropped",
       cfg_parse_subscription_ex(longline, strlen(longline), out, 16, &count, &stats) == CFG_OK &&
       count == 0 && stats.skipped == 1 && strcmp(stats.kinds[0].what, "link too long") == 0);

    /* the kind that dominates a feed may be the last one it shows */
    cfg_import_stats_t late;
    memset(&late, 0, sizeof late);
    cfg_import_stats_add(&late, "a");
    cfg_import_stats_add(&late, "b");
    cfg_import_stats_add(&late, "c");
    cfg_import_stats_add(&late, "d");
    cfg_import_stats_add(&late, "e");
    for (int i = 0; i < 5; ++i) cfg_import_stats_add(&late, "ssr");
    cfg_import_stats_text(&late, text, sizeof text);
    ok("a late common kind is named first", strcmp(text, "skipped 10: ssr 5, a 1, b 1, c 1, other 2") == 0);

    const char json[] =
        "[{\"remarks\":\"DE\",\"outbounds\":["
        "{\"protocol\":\"wireguard\",\"settings\":{}},"
        "{\"protocol\":\"freedom\"},"
        "{\"type\":\"tuic\",\"server\":\"example.com\",\"server_port\":443}]}]";
    ok("json outbounds senko cannot run are counted, plumbing is not",
       cfg_parse_subscription_ex(json, sizeof json - 1, out, 16, &count, &stats) == CFG_OK &&
       count == 0 && stats.skipped == 2 && strcmp(stats.kinds[0].what, "wireguard") == 0);

    /* base64 of the same kind of body, wrapped every 76 characters */
    const char wrapped[] =
        "dmxlc3M6Ly8xMTExMTExMS0xMTExLTQxMTEtODExMS0xMTExMTExMTExMTFAZXhhbXBsZS5j\n"
        "b206NDQzP3NlY3VyaXR5PXRscyZ0eXBlPXRjcCNvbmU=\n";
    ok("wrapped base64 body",
       cfg_parse_subscription_ex(wrapped, sizeof wrapped - 1, out, 16, &count, &stats) == CFG_OK &&
       count == 1 && strcmp(out[0].remark, "one") == 0);
}

/* hiddify style panels put profile headers above the body itself */
static void test_header_lines(void) {
    static vl_server_t out[16];
    size_t count = 0;
    cfg_import_stats_t stats;
    const char singbox[] =
        "//profile-title: base64:VEVTVA==\n"
        "//profile-update-interval: 1\n\n"
        "{\"outbounds\":[{\"type\":\"trojan\",\"tag\":\"t\",\"server\":\"a.example\","
        "\"server_port\":443,\"password\":\"pw\",\"tls\":{\"enabled\":true,"
        "\"server_name\":\"a.example\"}},{\"type\":\"direct\",\"tag\":\"direct\"}]}";
    ok("sing-box json behind // headers",
       cfg_parse_subscription_ex(singbox, sizeof singbox - 1, out, 16, &count, &stats) == CFG_OK &&
       count == 1 && strcmp(out[0].host, "a.example") == 0);
    const char b64[] =
        "#profile-title: base64:VEVTVA==\n"
        "#subscription-userinfo: upload=0; download=0; total=0; expire=0\n"
        "dHJvamFuOi8vcHdAYS5leGFtcGxlOjQ0Mz9zZWN1cml0eT10bHMjdA==\n";
    ok("base64 behind # headers",
       cfg_parse_subscription_ex(b64, sizeof b64 - 1, out, 16, &count, &stats) == CFG_OK &&
       count == 1 && strcmp(out[0].remark, "t") == 0);
}

static void test_raw_remarks(void) {
    static vl_server_t out[8];
    size_t count = 0;
    cfg_import_stats_t stats;
    const char feed[] =
        "vless://11111111-2222-3333-4444-555555555555@a.example:443?type=ws&security=tls"
        "&path=%2Fw#Hong Kong \xF0\x9F\x87\xAD\xF0\x9F\x87\xB0 \xE2\x86\x92 [x]\n"
        "trojan://pw@b.example:443?security=tls#one trojan://pw@c.example:443?security=tls#two\n";
    ok("unencoded remark keeps its spaces and flag",
       cfg_parse_subscription_ex(feed, sizeof feed - 1, out, 8, &count, &stats) == CFG_OK &&
       count == 3 &&
       strcmp(out[0].remark, "Hong Kong \xF0\x9F\x87\xAD\xF0\x9F\x87\xB0 \xE2\x86\x92 [x]") == 0);
    ok("a second link after a remark is still its own node",
       count == 3 && strcmp(out[1].remark, "one") == 0 &&
       strcmp(out[2].host, "c.example") == 0 && strcmp(out[2].remark, "two") == 0);
}

/* each outbound on its own: the reason a node is skipped, or the node */
static const char *json_one(const char *outbound, vl_server_t *node) {
    static char body[2048];
    static char text[160];
    static vl_server_t out[4];
    size_t count = 0;
    cfg_import_stats_t stats;
    snprintf(body, sizeof body, "{\"outbounds\":[%s]}", outbound);
    if (cfg_parse_subscription_ex(body, strlen(body), out, 4, &count, &stats) != CFG_OK)
        return "parse error";
    if (count == 1) {
        if (node) *node = out[0];
        return "ok";
    }
    snprintf(text, sizeof text, "%s", stats.skipped == 1 ? stats.kinds[0].what : "nothing");
    return text;
}

static void test_json_outbounds(void) {
    vl_server_t n;
    ok("xray vless reality",
       strcmp(json_one("{\"protocol\":\"vless\",\"settings\":{\"vnext\":[{\"address\":\"a.example\","
                       "\"port\":443,\"users\":[{\"id\":\"" UUID "\",\"flow\":\"xtls-rprx-vision\","
                       "\"encryption\":\"none\"}]}]},\"streamSettings\":{\"network\":\"tcp\","
                       "\"security\":\"reality\",\"realitySettings\":{\"serverName\":\"b.example\","
                       "\"publicKey\":\"" PBK "\",\"shortId\":\"ab\",\"fingerprint\":\"chrome\"}}}", &n),
              "ok") == 0 &&
       n.security == VL_SEC_REALITY && strcmp(n.sni, "b.example") == 0 && strcmp(n.sid, "ab") == 0);
    ok("xray settings without vnext",
       strcmp(json_one("{\"protocol\":\"vless\",\"settings\":{\"address\":\"a.example\",\"port\":443,"
                       "\"id\":\"" UUID "\",\"encryption\":\"none\"},"
                       "\"streamSettings\":{\"security\":\"tls\"}}", &n), "ok") == 0 &&
       strcmp(n.host, "a.example") == 0 && n.security == VL_SEC_TLS);
    ok("xray tcp http header is named",
       strcmp(json_one("{\"protocol\":\"vless\",\"settings\":{\"vnext\":[{\"address\":\"a.example\","
                       "\"port\":80,\"users\":[{\"id\":\"" UUID "\"}]}]},\"streamSettings\":{"
                       "\"network\":\"tcp\",\"tcpSettings\":{\"header\":{\"type\":\"http\"}}}}", NULL),
              "tcp with an http header") == 0);
    ok("xray httpupgrade is not h2",
       strcmp(json_one("{\"protocol\":\"vless\",\"settings\":{\"vnext\":[{\"address\":\"a.example\","
                       "\"port\":80,\"users\":[{\"id\":\"" UUID "\"}]}]},\"streamSettings\":{"
                       "\"network\":\"httpupgrade\"}}", NULL), "httpupgrade transport") == 0);
    ok("xray trojan over grpc is refused, not run as tcp",
       strcmp(json_one("{\"protocol\":\"trojan\",\"settings\":{\"servers\":[{\"address\":\"a.example\","
                       "\"port\":443,\"password\":\"pw\"}]},\"streamSettings\":{\"network\":\"grpc\","
                       "\"security\":\"tls\"}}", NULL), "unsupported trojan transport") == 0);
    ok("xray trojan keeps allowInsecure",
       strcmp(json_one("{\"protocol\":\"trojan\",\"settings\":{\"servers\":[{\"address\":\"a.example\","
                       "\"port\":443,\"password\":\"pw\"}]},\"streamSettings\":{\"security\":\"tls\","
                       "\"tlsSettings\":{\"allowInsecure\":true}}}", &n), "ok") == 0 && n.insecure);
    ok("xray shadowsocks method spelling",
       strcmp(json_one("{\"protocol\":\"shadowsocks\",\"settings\":{\"servers\":[{\"address\":\"a.example\","
                       "\"port\":8388,\"method\":\"chacha20-poly1305\",\"password\":\"pw\"}]}}", &n),
              "ok") == 0 && strcmp(n.encryption, "chacha20-ietf-poly1305") == 0);
    ok("xray socks",
       strcmp(json_one("{\"protocol\":\"socks\",\"settings\":{\"servers\":[{\"address\":\"a.example\","
                       "\"port\":1080,\"users\":[{\"user\":\"u\",\"pass\":\"p\"}]}]}}", &n), "ok") == 0 &&
       n.proto == VL_PROTO_SOCKS5 && strcmp(n.pass, "p") == 0);
    ok("xray wireguard is named",
       strcmp(json_one("{\"protocol\":\"wireguard\",\"settings\":{}}", NULL), "wireguard") == 0);
    ok("xray freedom is plumbing", strcmp(json_one("{\"protocol\":\"freedom\"}", NULL), "nothing") == 0);
    ok("xray vless without id",
       strcmp(json_one("{\"protocol\":\"vless\",\"settings\":{\"vnext\":[{\"address\":\"a.example\","
                       "\"port\":443,\"users\":[]}]}}", NULL), "malformed profile entry") == 0);

    ok("sing-box trojan over grpc is refused, not run as tcp",
       strcmp(json_one("{\"type\":\"trojan\",\"server\":\"a.example\",\"server_port\":443,"
                       "\"password\":\"pw\",\"tls\":{\"enabled\":true},"
                       "\"transport\":{\"type\":\"grpc\",\"service_name\":\"s\"}}", NULL),
              "unsupported trojan transport") == 0);
    ok("sing-box vless ws",
       strcmp(json_one("{\"type\":\"vless\",\"server\":\"a.example\",\"server_port\":443,"
                       "\"uuid\":\"" UUID "\",\"tls\":{\"enabled\":true,\"server_name\":\"b.example\","
                       "\"insecure\":true,\"utls\":{\"enabled\":true,\"fingerprint\":\"chrome\"}},"
                       "\"transport\":{\"type\":\"ws\",\"path\":\"/p\",\"headers\":{\"Host\":\"c.example\"}}}",
                       &n), "ok") == 0 &&
       n.net == VL_NET_WS && strcmp(n.ws_host, "c.example") == 0 && strcmp(n.sni, "b.example") == 0 &&
       n.insecure && strcmp(n.fp, "chrome") == 0);
    ok("sing-box httpupgrade is named",
       strcmp(json_one("{\"type\":\"vless\",\"server\":\"a.example\",\"server_port\":443,"
                       "\"uuid\":\"" UUID "\",\"transport\":{\"type\":\"httpupgrade\"}}", NULL),
              "httpupgrade transport") == 0);
    ok("sing-box shadowsocks plugin is named",
       strcmp(json_one("{\"type\":\"shadowsocks\",\"server\":\"a.example\",\"server_port\":8388,"
                       "\"method\":\"aes-128-gcm\",\"password\":\"pw\",\"plugin\":\"obfs-local\"}", NULL),
              "shadowsocks plugin obfs-local") == 0);
    ok("sing-box shadowsocks ignores a stray tls block",
       strcmp(json_one("{\"type\":\"shadowsocks\",\"server\":\"a.example\",\"server_port\":8388,"
                       "\"method\":\"aes-128-gcm\",\"password\":\"pw\",\"tls\":{\"enabled\":true}}", &n),
              "ok") == 0 && n.security == VL_SEC_NONE);
    ok("sing-box hysteria2 port range",
       strcmp(json_one("{\"type\":\"hysteria2\",\"server\":\"a.example\",\"server_ports\":[\"20000:30000\"],"
                       "\"password\":\"pw\",\"tls\":{\"enabled\":true,\"server_name\":\"a.example\"}}", &n),
              "ok") == 0 && n.port == 20000 && strcmp(n.port_hop, "20000-30000") == 0);
    ok("sing-box tuic is named",
       strcmp(json_one("{\"type\":\"tuic\",\"server\":\"a.example\",\"server_port\":443}", NULL), "tuic") == 0);
    ok("sing-box selector is plumbing",
       strcmp(json_one("{\"type\":\"selector\",\"outbounds\":[\"a\"]}", NULL), "nothing") == 0);
    ok("shadowsocks link over ws is refused",
       refused_for("ss://YWVzLTEyOC1nY206cHc@a.example:8388?type=ws#s", "shadowsocks over another transport"));
}

/* clash entries go through the same node rules as links and json */
static void test_clash_entries(void) {
    static vl_server_t out[8];
    size_t count = 0;
    cfg_import_stats_t stats;
    char text[160];
    const char yaml[] =
        "proxies:\n"
        "  - {name: a, type: trojan, server: a.example, port: 443, password: pw, network: h2}\n"
        "  - {name: b, type: vless, server: b.example, port: 80, uuid: " UUID ", network: http}\n"
        "  - {name: c, type: socks5, server: c.example, port: 1080, tls: true}\n"
        "  - {name: d, type: vless, server: d.example, port: 443, uuid: 11111111111141118111111111111111, tls: true, network: ws, ws-opts: {path: /p}}\n";
    ok("clash body parses",
       cfg_parse_subscription_ex(yaml, sizeof yaml - 1, out, 8, &count, &stats) == CFG_OK);
    cfg_import_stats_text(&stats, text, sizeof text);
    ok("clash trojan over h2 is refused, not run as tcp", strstr(text, "unsupported trojan transport 1") != NULL);
    ok("clash network http is the tcp header disguise", strstr(text, "tcp with an http header 1") != NULL);
    ok("clash socks over tls is refused", strstr(text, "socks over tls 1") != NULL);
    ok("clash vless ws with a dashless id",
       count == 1 && out[0].net == VL_NET_WS && strcmp(out[0].uuid, UUID) == 0 &&
       strcmp(out[0].ws_host, "d.example") == 0);
}

/* block style items as clash exporters sort them: a list valued key first */
static void test_clash_nested_lists(void) {
    static vl_server_t out[8];
    size_t count = 0;
    cfg_import_stats_t stats;
    const char yaml[] =
        "proxies:\n"
        "- alpn:\n"
        "  - h3\n"
        "  name: hy\n"
        "  obfs: salamander\n"
        "  obfs-password: op\n"
        "  password: pw\n"
        "  port: 44100\n"
        "  server: a.example\n"
        "  sni: a.example\n"
        "  type: hysteria2\n"
        "- ws-opts:\n"
        "    path: /p\n"
        "    headers:\n"
        "      Host: h.example\n"
        "  name: ws\n"
        "  network: ws\n"
        "  password: pw\n"
        "  port: 443\n"
        "  server: b.example\n"
        "  type: trojan\n"
        "proxy-groups:\n"
        "- name: g\n";
    ok("clash items with nested lists and maps first",
       cfg_parse_subscription_ex(yaml, sizeof yaml - 1, out, 8, &count, &stats) == CFG_OK &&
       count == 2 && stats.skipped == 0 &&
       out[0].proto == VL_PROTO_HYSTERIA2 && out[0].port == 44100 &&
       strcmp(out[0].obfs_password, "op") == 0 &&
       out[1].net == VL_NET_WS && strcmp(out[1].path, "/p") == 0 &&
       strcmp(out[1].ws_host, "h.example") == 0);
}

/* what the parser reads must survive being saved and read back */
/* v2rayn base64 json, the xtls share link form, and the json profile shapes */
static void test_vmess_links(void) {
    ok("v2rayn ws over tls",
       usable("vmess://eyJ2IjoiMiIsInBzIjoianAgd3MiLCJhZGQiOiJqcC5leGFtcGxlIiwicG9ydCI6IjQ0MyIsImlkIjoiMTExMTExMTEtMTExMS00MTExLTgxMTEtMTExMTExMTExMTExIiwiYWlkIjoiMCIsInNjeSI6ImNoYWNoYTIwLXBvbHkxMzA1IiwibmV0Ijoid3MiLCJ0eXBlIjoibm9uZSIsImhvc3QiOiJjZG4uZXhhbXBsZSIsInBhdGgiOiIvdm0iLCJ0bHMiOiJ0bHMiLCJzbmkiOiJzbmkuZXhhbXBsZSIsImZwIjoiY2hyb21lIn0=") && s.proto == VL_PROTO_VMESS &&
       strcmp(s.uuid, UUID) == 0 && strcmp(s.encryption, "chacha20-poly1305") == 0 &&
       strcmp(s.user, "chacha20-poly1305") == 0 && s.net == VL_NET_WS &&
       strcmp(s.path, "/vm") == 0 && strcmp(s.ws_host, "cdn.example") == 0 &&
       s.security == VL_SEC_TLS && strcmp(s.sni, "sni.example") == 0 &&
       strcmp(s.fp, "chrome") == 0 && strcmp(s.remark, "jp ws") == 0);
    ok("v2rayn numeric port, no cipher, no tls",
       usable("vmess://eyJ2IjoiMiIsInBzIjoiIiwiYWRkIjoiMTAuMC4wLjEiLCJwb3J0Ijo4MDgwLCJpZCI6IjExMTExMTExLTExMTEtNDExMS04MTExLTExMTExMTExMTExMSIsImFpZCI6MCwibmV0IjoidGNwIiwidHlwZSI6Im5vbmUiLCJ0bHMiOiIifQ==#named") && s.port == 8080 && s.net == VL_NET_TCP &&
       s.security == VL_SEC_NONE && strcmp(s.encryption, "auto") == 0 &&
       strcmp(s.remark, "named") == 0);
    ok("v2rayn grpc multi",
       usable("vmess://eyJ2IjoiMiIsInBzIjoiZyIsImFkZCI6ImcuZXhhbXBsZSIsInBvcnQiOiI0NDMiLCJpZCI6IjExMTExMTExLTExMTEtNDExMS04MTExLTExMTExMTExMTExMSIsIm5ldCI6ImdycGMiLCJ0eXBlIjoibXVsdGkiLCJwYXRoIjoic3ZjIiwidGxzIjoidGxzIn0=") && s.net == VL_NET_GRPC && s.grpc_multi);
    ok("v2rayn tcp http header is named",
       refused_for("vmess://eyJ2IjoiMiIsInBzIjoiaCIsImFkZCI6ImguZXhhbXBsZSIsInBvcnQiOiI4MCIsImlkIjoiMTExMTExMTEtMTExMS00MTExLTgxMTEtMTExMTExMTExMTExIiwibmV0IjoidGNwIiwidHlwZSI6Imh0dHAifQ==", "http header"));
    ok("v2rayn kcp is named", refused_for("vmess://eyJ2IjoiMiIsInBzIjoiayIsImFkZCI6ImsuZXhhbXBsZSIsInBvcnQiOiI4MCIsImlkIjoiMTExMTExMTEtMTExMS00MTExLTgxMTEtMTExMTExMTExMTExIiwibmV0Ijoia2NwIn0=", "kcp"));
    ok("v2rayn body without an id", !usable("vmess://eyJ2IjoiMiIsInBzIjoibiIsImFkZCI6Im4uZXhhbXBsZSIsInBvcnQiOiI4MCJ9"));
    ok("a body that is not base64", !usable("vmess://%%%%not-base64%%%%"));
    ok("xtls share link form",
       usable("vmess://" UUID "@example.com:443?encryption=aes-128-gcm&security=reality&pbk=" PBK
              "&sid=ab&type=tcp#r") &&
       s.proto == VL_PROTO_VMESS && s.security == VL_SEC_REALITY &&
       strcmp(s.encryption, "aes-128-gcm") == 0 && strcmp(s.remark, "r") == 0);
    ok("xtls form without encryption runs auto",
       usable("vmess://" UUID "@example.com:80?type=ws&path=%2F#w") &&
       strcmp(s.encryption, "auto") == 0 && s.net == VL_NET_WS);
    ok("vmess carries no flow",
       refused_for("vmess://" UUID "@example.com:443?security=tls&flow=xtls-rprx-vision#f", "flow"));
    ok("vmess needs an id", !usable("vmess://@example.com:443?security=tls#f"));

    vl_server_t n;
    ok("xray vmess vnext",
       strcmp(json_one("{\"protocol\":\"vmess\",\"settings\":{\"vnext\":[{\"address\":\"a.example\","
                       "\"port\":443,\"users\":[{\"id\":\"" UUID "\",\"alterId\":0,"
                       "\"security\":\"aes-128-gcm\"}]}]},\"streamSettings\":{\"network\":\"ws\","
                       "\"security\":\"tls\",\"wsSettings\":{\"path\":\"/p\"}}}", &n), "ok") == 0 &&
       n.proto == VL_PROTO_VMESS && strcmp(n.encryption, "aes-128-gcm") == 0 &&
       n.net == VL_NET_WS && n.security == VL_SEC_TLS && strcmp(n.path, "/p") == 0);
    ok("xray vmess flat settings",
       strcmp(json_one("{\"protocol\":\"vmess\",\"settings\":{\"address\":\"a.example\","
                       "\"port\":443,\"id\":\"" UUID "\"}}", &n), "ok") == 0 &&
       n.proto == VL_PROTO_VMESS && strcmp(n.encryption, "auto") == 0);
    ok("xray vmess without an id",
       strcmp(json_one("{\"protocol\":\"vmess\",\"settings\":{\"vnext\":[{\"address\":\"a.example\","
                       "\"port\":443,\"users\":[]}]}}", NULL), "malformed profile entry") == 0);
    ok("sing-box vmess grpc over tls",
       strcmp(json_one("{\"type\":\"vmess\",\"server\":\"a.example\",\"server_port\":443,"
                       "\"uuid\":\"" UUID "\",\"security\":\"chacha20-poly1305\",\"alter_id\":0,"
                       "\"transport\":{\"type\":\"grpc\",\"service_name\":\"svc\"},"
                       "\"tls\":{\"enabled\":true,\"server_name\":\"b.example\"}}", &n), "ok") == 0 &&
       n.proto == VL_PROTO_VMESS && n.net == VL_NET_GRPC && n.security == VL_SEC_TLS &&
       strcmp(n.sni, "b.example") == 0 && strcmp(n.encryption, "chacha20-poly1305") == 0);
}

static void test_roundtrip(void) {
    static const char *const links[] = {
        "trojan://p%40s%3As%23s%2Fs%3Fs@example.com:443?security=tls&sni=example.com#t",
        "socks5://us%3Aer:pa%40ss@example.com:1080#s",
        "vless://my-user-7@example.com:443?security=tls&type=ws&path=%2Fp#v",
        "hysteria2://au%3Ath%40x@example.com:443?sni=example.com#h",
        "ss://YWVzLTI1Ni1nY206cGE6c3NAd29yZA@example.com:8388#ss",
        "vmess://eyJ2IjoiMiIsInBzIjoianAgd3MiLCJhZGQiOiJqcC5leGFtcGxlIiwicG9ydCI6IjQ0MyIsImlkIjoiMTExMTExMTEtMTExMS00MTExLTgxMTEtMTExMTExMTExMTExIiwiYWlkIjoiMCIsInNjeSI6ImNoYWNoYTIwLXBvbHkxMzA1IiwibmV0Ijoid3MiLCJ0eXBlIjoibm9uZSIsImhvc3QiOiJjZG4uZXhhbXBsZSIsInBhdGgiOiIvdm0iLCJ0bHMiOiJ0bHMiLCJzbmkiOiJzbmkuZXhhbXBsZSIsImZwIjoiY2hyb21lIn0=",
        "vmess://my-user-7@example.com:443?encryption=aes-128-gcm&security=tls&type=grpc&serviceName=svc#g",
    };
    static store_t a, b;
    static char buf[64 * 1024];
    store_init(&a);
    store_init(&b);
    for (size_t i = 0; i < sizeof links / sizeof links[0]; ++i)
        ok(links[i], store_add_manual(&a, links[i], NULL) == STORE_OK);
    size_t len = 0;
    ok("saved", store_serialize(&a, buf, sizeof buf, &len) == STORE_OK);
    ok("read back", store_deserialize(&b, buf, len) == STORE_OK && b.n == a.n);
    for (size_t i = 0; i < a.n && i < b.n; ++i) {
        const vl_server_t *x = &a.servers[i], *y = &b.servers[i];
        ok(links[i], strcmp(x->pass, y->pass) == 0 && strcmp(x->user, y->user) == 0 &&
                     strcmp(x->uuid, y->uuid) == 0 && strcmp(x->host, y->host) == 0 &&
                     x->port == y->port && strcmp(x->remark, y->remark) == 0);
    }
    ok("trojan password kept", strcmp(a.servers[0].pass, "p@s:s#s/s?s") == 0);
    ok("socks login kept", strcmp(a.servers[1].user, "us:er") == 0 &&
                           strcmp(a.servers[1].pass, "pa@ss") == 0);
    ok("ss password with a colon kept", strcmp(a.servers[4].pass, "pa:ss@word") == 0);
    for (size_t i = 5; i < 7 && i < b.n; ++i) {
        const vl_server_t *x = &a.servers[i], *y = &b.servers[i];
        ok("vmess kept as vmess", y->proto == VL_PROTO_VMESS && x->proto == VL_PROTO_VMESS);
        ok("vmess transport and cipher kept",
           x->net == y->net && x->security == y->security &&
           strcmp(x->encryption, y->encryption) == 0 && strcmp(x->path, y->path) == 0 &&
           strcmp(x->ws_host, y->ws_host) == 0 && strcmp(x->sni, y->sni) == 0 &&
           strcmp(x->fp, y->fp) == 0 && x->grpc_multi == y->grpc_multi);
    }
    ok("vmess cipher survives", b.n > 6 && strcmp(b.servers[6].encryption, "aes-128-gcm") == 0 &&
       b.servers[6].net == VL_NET_GRPC);
}

int main(void) {
    test_roundtrip();
    test_vless();
    test_trojan();
    test_shadowsocks();
    test_socks_and_hysteria();
    test_vmess_links();
    test_unsupported_schemes();
    test_subscription();
    test_header_lines();
    test_raw_remarks();
    test_json_outbounds();
    test_clash_entries();
    test_clash_nested_lists();
    if (g_fail) {
        fprintf(stderr, "%d share link check(s) failed\n", g_fail);
        return 1;
    }
    puts("all share link checks passed");
    return 0;
}
