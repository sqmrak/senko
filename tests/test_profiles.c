/* clash and shadowrocket profile import. the fixtures follow the shapes real
   panels emit, including the quoted names with flag emoji that broke naive
   splitting */
#include "profiles.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void ok(const char *what, int cond) {
    if (!cond) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static const char kClash[] =
"mixed-port: 7890\n"
"allow-lan: true\n"
"dns:\n"
"  enable: true\n"
"  nameserver:\n"
"    - 1.1.1.1\n"
"proxies:\n"
"  - name: \"\xF0\x9F\x87\xB3\xF0\x9F\x87\xB1 NL reality\"\n"
"    type: vless\n"
"    server: nl.example.com\n"
"    port: 443\n"
"    uuid: 11111111-2222-3333-4444-555555555555\n"
"    network: tcp\n"
"    udp: true\n"
"    flow: xtls-rprx-vision\n"
"    client-fingerprint: chrome\n"
"    tls: true\n"
"    servername: www.microsoft.com\n"
"    reality-opts:\n"
"      public-key: 6Xr3aBcDeFgHiJkLmNoPqRsTuVwXyZ0123456789abc\n"
"      short-id: 0a1b\n"
"  - name: ws node\n"
"    type: vless\n"
"    server: 203.0.113.9\n"
"    port: 8443\n"
"    uuid: aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\n"
"    tls: true\n"
"    network: ws\n"
"    ws-opts:\n"
"      path: /ray\n"
"      headers:\n"
"        Host: cdn.example.net\n"
"  - name: ss node\n"
"    type: ss\n"
"    server: 198.51.100.4\n"
"    port: 8388\n"
"    cipher: aes-128-gcm\n"
"    password: secret\n"
"  - name: unrunnable node\n"
"    type: vmess\n"
"    server: 198.51.100.5\n"
"    port: 8389\n"
"  - name: hy2 node\n"
"    type: hysteria2\n"
"    server: hy.example.com\n"
"    port: 44300\n"
"    password: hy2pw\n"
"    obfs: salamander\n"
"    obfs-password: obfspw\n"
"    sni: hy2.sni.example\n"
"    ports: 44300,45000-46000\n"
"  - {name: flow style, type: socks5, server: 192.0.2.7, port: 1080, username: u, password: p}\n"
"proxy-groups:\n"
"  - name: sel\n"
"    type: select\n";

static const char kSurge[] =
"#!MANAGED-CONFIG interval=86400\n"
"[General]\n"
"bypass-system = true\n"
"[Proxy]\n"
"\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8 US = vless, us.example.org, 443, password=99999999-8888-7777-6666-555555555555, obfs=websocket, obfs-host=edge.example.org, obfs-uri=/vl, tls=1, peer=edge.example.org\n"
"local socks = socks5, 192.0.2.1, 1080, alice, s3cret\n"
"runnable trojan = trojan, t.example.org, 443, password=x\n"
"hy2 = hysteria2, hy.example.org, 44300, password=hy2pw, obfs=salamander, "
"obfs-password=obfspw, sni=hy2.sni.example, ports=45000-46000\n"
"broken = vmess, v.example.org, 443, username=x\n"
"[Rule]\n"
"FINAL,PROXY\n";

int main(void) {
    vl_server_t servers[16];
    size_t n;

    ok("clash detected", profiles_looks_like_clash(kClash, sizeof kClash - 1));
    ok("clash is not surge", !profiles_looks_like_surge(kClash, sizeof kClash - 1));
    ok("content kind clash",
       cfg_content_kind(kClash, sizeof kClash - 1) == CFG_CONTENT_CLASH);

    memset(servers, 0, sizeof servers);
    n = profiles_parse_clash(kClash, sizeof kClash - 1, servers, 16);
    ok("clash keeps only runnable nodes", n == 5);
    if (n >= 1) {
        ok("clash reality host", strcmp(servers[0].host, "nl.example.com") == 0);
        ok("clash reality port", servers[0].port == 443);
        ok("clash reality security", servers[0].security == VL_SEC_REALITY);
        ok("clash reality pbk",
           strcmp(servers[0].pbk, "6Xr3aBcDeFgHiJkLmNoPqRsTuVwXyZ0123456789abc") == 0);
        ok("clash reality sid", strcmp(servers[0].sid, "0a1b") == 0);
        ok("clash reality sni", strcmp(servers[0].sni, "www.microsoft.com") == 0);
        ok("clash reality flow", strcmp(servers[0].flow, "xtls-rprx-vision") == 0);
        ok("clash quoted name survives",
           strcmp(servers[0].remark, "\xF0\x9F\x87\xB3\xF0\x9F\x87\xB1 NL reality") == 0);
    }
    if (n >= 2) {
        ok("clash ws network", servers[1].net == VL_NET_WS);
        ok("clash ws path", strcmp(servers[1].path, "/ray") == 0);
        ok("clash ws host", strcmp(servers[1].ws_host, "cdn.example.net") == 0);
        ok("clash ws tls", servers[1].security == VL_SEC_TLS);
    }
    if (n >= 3) {
        ok("clash ss proto", servers[2].proto == VL_PROTO_SHADOWSOCKS);
        ok("clash ss cipher", strcmp(servers[2].encryption, "aes-128-gcm") == 0);
        ok("clash ss pass", strcmp(servers[2].pass, "secret") == 0);
    }
    if (n >= 4) {
        ok("clash hy2 proto", servers[3].proto == VL_PROTO_HYSTERIA2);
        ok("clash hy2 host", strcmp(servers[3].host, "hy.example.com") == 0);
        ok("clash hy2 port", servers[3].port == 44300);
        ok("clash hy2 pass", strcmp(servers[3].pass, "hy2pw") == 0);
        ok("clash hy2 sni", strcmp(servers[3].sni, "hy2.sni.example") == 0);
        ok("clash hy2 obfs", strcmp(servers[3].obfs, "salamander") == 0);
        ok("clash hy2 obfs password", strcmp(servers[3].obfs_password, "obfspw") == 0);
        ok("clash hy2 port hop",
           strcmp(servers[3].port_hop, "44300,45000-46000") == 0);
    }
    if (n >= 5) {
        ok("clash flow style socks", servers[4].proto == VL_PROTO_SOCKS5);
        ok("clash flow style host", strcmp(servers[4].host, "192.0.2.7") == 0);
        ok("clash flow style port", servers[4].port == 1080);
        ok("clash flow style user", strcmp(servers[4].user, "u") == 0);
        ok("clash flow style pass", strcmp(servers[4].pass, "p") == 0);
    }

    ok("surge detected", profiles_looks_like_surge(kSurge, sizeof kSurge - 1));
    ok("content kind surge",
       cfg_content_kind(kSurge, sizeof kSurge - 1) == CFG_CONTENT_SURGE);
    memset(servers, 0, sizeof servers);
    n = profiles_parse_surge(kSurge, sizeof kSurge - 1, servers, 16);
    ok("surge keeps only runnable nodes", n == 4);
    if (n >= 1) {
        ok("surge vless host", strcmp(servers[0].host, "us.example.org") == 0);
        ok("surge vless uuid",
           strcmp(servers[0].uuid, "99999999-8888-7777-6666-555555555555") == 0);
        ok("surge vless ws", servers[0].net == VL_NET_WS);
        ok("surge vless path", strcmp(servers[0].path, "/vl") == 0);
        ok("surge vless ws host", strcmp(servers[0].ws_host, "edge.example.org") == 0);
        ok("surge vless tls", servers[0].security == VL_SEC_TLS);
        ok("surge vless sni", strcmp(servers[0].sni, "edge.example.org") == 0);
    }
    if (n >= 2) {
        ok("surge socks proto", servers[1].proto == VL_PROTO_SOCKS5);
        ok("surge socks user", strcmp(servers[1].user, "alice") == 0);
        ok("surge socks pass", strcmp(servers[1].pass, "s3cret") == 0);
    }
    if (n >= 3) {
        ok("surge trojan proto", servers[2].proto == VL_PROTO_TROJAN);
        ok("surge trojan pass", strcmp(servers[2].pass, "x") == 0);
    }
    if (n >= 4) {
        ok("surge hy2 proto", servers[3].proto == VL_PROTO_HYSTERIA2);
        ok("surge hy2 host", strcmp(servers[3].host, "hy.example.org") == 0);
        ok("surge hy2 port", servers[3].port == 44300);
        ok("surge hy2 pass", strcmp(servers[3].pass, "hy2pw") == 0);
        ok("surge hy2 sni", strcmp(servers[3].sni, "hy2.sni.example") == 0);
        ok("surge hy2 obfs", strcmp(servers[3].obfs, "salamander") == 0);
        ok("surge hy2 obfs password", strcmp(servers[3].obfs_password, "obfspw") == 0);
        ok("surge hy2 port hop", strcmp(servers[3].port_hop, "45000-46000") == 0);
    }

    /* the subscription entry point has to route both profile formats */
    memset(servers, 0, sizeof servers);
    n = 0;
    ok("clash through subscription parser",
       cfg_parse_subscription(kClash, sizeof kClash - 1, servers, 16, &n) == CFG_OK &&
       n == 5);
    memset(servers, 0, sizeof servers);
    n = 0;
    ok("surge through subscription parser",
       cfg_parse_subscription(kSurge, sizeof kSurge - 1, servers, 16, &n) == CFG_OK &&
       n == 4);

    /* classification the ui turns into "unknown content type" */
    {
        static const char junk[] = "this is just a note to self\nnothing here\n";
        static const char links[] =
            "vless://11111111-2222-3333-4444-555555555555@a.example:443"
            "?security=tls&type=tcp&sni=a.example#one\n";
        static const char b64[] =
            "dmxlc3M6Ly8xMTExMTExMS0yMjIyLTMzMzMtNDQ0NC01NTU1NTU1NTU1NTVAYS5leGFtcGxl"
            "OjQ0Mz9zZWN1cml0eT10bHMmdHlwZT10Y3Amc25pPWEuZXhhbXBsZSNvbmUK";
        ok("junk is unknown",
           cfg_content_kind(junk, sizeof junk - 1) == CFG_CONTENT_UNKNOWN);
        ok("links detected",
           cfg_content_kind(links, sizeof links - 1) == CFG_CONTENT_LINKS);
        ok("base64 detected",
           cfg_content_kind(b64, sizeof b64 - 1) == CFG_CONTENT_BASE64);
        ok("empty is unknown", cfg_content_kind("", 0) == CFG_CONTENT_UNKNOWN);
    }

    if (failures) {
        printf("%d profile check(s) failed\n", failures);
        return 1;
    }
    printf("all profile import checks passed\n");
    return 0;
}
