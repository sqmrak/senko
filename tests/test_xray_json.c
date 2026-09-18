#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
static void ok(const char *what, int cond) {
    if (cond) return;
    g_fail++;
    fprintf(stderr, "FAIL %s\n", what);
}

/* 32-byte zero key, standard/url-safe friendly base64 (43 chars + pad omitted ok) */
#define PBK "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"

static const char k_array_cfg[] =
    "["
    "  {"
    "    \"remarks\": \"Liberty Auto\","
    "    \"outbounds\": ["
    "      {"
    "        \"protocol\": \"vless\","
    "        \"tag\": \"proxy-a\","
    "        \"settings\": {"
    "          \"vnext\": [{"
    "            \"address\": \"1.2.3.4\","
    "            \"port\": 443,"
    "            \"users\": [{"
    "              \"id\": \"11111111-1111-4111-8111-111111111111\","
    "              \"encryption\": \"none\","
    "              \"flow\": \"xtls-rprx-vision\""
    "            }]"
    "          }]"
    "        },"
    "        \"streamSettings\": {"
    "          \"network\": \"tcp\","
    "          \"security\": \"reality\","
    "          \"realitySettings\": {"
    "            \"serverName\": \"cdn.example\","
    "            \"fingerprint\": \"firefox\","
    "            \"publicKey\": \"" PBK "\","
    "            \"shortId\": \"abcd\""
    "          }"
    "        }"
    "      },"
    "      {"
    "        \"protocol\": \"freedom\","
    "        \"tag\": \"direct\""
    "      },"
    "      {"
    "        \"protocol\": \"vless\","
    "        \"tag\": \"proxy-b\","
    "        \"settings\": {"
    "          \"vnext\": [{"
    "            \"address\": \"5.6.7.8\","
    "            \"port\": 8443,"
    "            \"users\": [{"
    "              \"id\": \"22222222-2222-4222-8222-222222222222\","
    "              \"encryption\": \"none\","
    "              \"flow\": \"xtls-rprx-vision\""
    "            }]"
    "          }]"
    "        },"
    "        \"streamSettings\": {"
    "          \"network\": \"tcp\","
    "          \"security\": \"reality\","
    "          \"realitySettings\": {"
    "            \"serverName\": \"edge.example\","
    "            \"fingerprint\": \"chrome\","
    "            \"publicKey\": \"" PBK "\","
    "            \"shortId\": [\"11\", \"22\"]"
    "          }"
    "        }"
    "      },"
    "      {"
    "        \"protocol\": \"vless\","
    "        \"tag\": \"grpc-skip\","
    "        \"settings\": {"
    "          \"vnext\": [{"
    "            \"address\": \"9.9.9.9\","
    "            \"port\": 443,"
    "            \"users\": [{"
    "              \"id\": \"33333333-3333-4333-8333-333333333333\","
    "              \"encryption\": \"none\""
    "            }]"
    "          }]"
    "        },"
    "        \"streamSettings\": {"
    "          \"network\": \"grpc\","
    "          \"security\": \"tls\","
    "          \"tlsSettings\": { \"serverName\": \"g.example\" },"
    "          \"grpcSettings\": { \"serviceName\": \"edge\" }"
    "        }"
    "      }"
    "    ]"
    "  },"
    "  {"
    "    \"remarks\": \"XHTTP node\","
    "    \"outbounds\": [{"
    "      \"protocol\": \"vless\","
    "      \"settings\": {"
    "        \"vnext\": [{"
    "          \"address\": \"xhttp.example\","
    "          \"port\": 443,"
    "          \"users\": [{"
    "            \"id\": \"44444444-4444-4444-8444-444444444444\","
    "            \"encryption\": \"none\","
    "            \"flow\": \"\""
    "          }]"
    "        }]"
    "      },"
    "      \"streamSettings\": {"
    "        \"network\": \"xhttp\","
    "        \"security\": \"tls\","
    "        \"tlsSettings\": { \"serverName\": \"sni.example\" },"
    "        \"xhttpSettings\": {"
    "          \"path\": \"/api/\","
    "          \"mode\": \"packet-up\","
    "          \"host\": \"host.example\""
    "        }"
    "      }"
    "    }]"
    "  },"
    "  {"
    "    \"remarks\": \"Trojan and SS\","
    "    \"outbounds\": ["
    "      {"
    "        \"protocol\": \"trojan\","
    "        \"tag\": \"trojan-ws\","
    "        \"settings\": {"
    "          \"servers\": [{"
    "            \"address\": \"trojan.example\","
    "            \"port\": 443,"
    "            \"password\": \"secretpw\""
    "          }]"
    "        },"
    "        \"streamSettings\": {"
    "          \"network\": \"ws\","
    "          \"tlsSettings\": { \"serverName\": \"trojan.sni.example\" },"
    "          \"wsSettings\": { \"path\": \"/tws\" }"
    "        }"
    "      },"
    "      {"
    "        \"protocol\": \"shadowsocks\","
    "        \"tag\": \"ss-node\","
    "        \"settings\": {"
    "          \"servers\": [{"
    "            \"address\": \"ss.example\","
    "            \"port\": 8388,"
    "            \"method\": \"chacha20-ietf-poly1305\","
    "            \"password\": \"sspass\""
    "          }]"
    "        }"
    "      }"
    "    ]"
    "  },"
    "  {"
    "    \"remarks\": \"Hysteria2\","
    "    \"outbounds\": [{"
    "      \"protocol\": \"hysteria\","
    "      \"settings\": { \"version\": 2, \"address\": \"hy.example\", \"port\": 44300 },"
    "      \"streamSettings\": {"
    "        \"network\": \"hysteria\","
    "        \"security\": \"tls\","
    "        \"tlsSettings\": {"
    "          \"serverName\": \"hy.sni.example\","
    "          \"pinnedPeerCertSha256\": \"AA:BB\""
    "        },"
    "        \"hysteriaSettings\": { \"version\": 2, \"auth\": \"hypw\" },"
    "        \"finalmask\": {"
    "          \"udp\": [{ \"type\": \"salamander\", \"settings\": { \"password\": \"obfspw\" } }],"
    "          \"quicParams\": { \"udpHop\": { \"ports\": \"44300,45000-46000\" } }"
    "        }"
    "      }"
    "    }]"
    "  }"
    "]";

static const char k_single_cfg[] =
    "{"
    "  \"remarks\": \"solo\","
    "  \"outbounds\": [{"
    "    \"protocol\": \"vless\","
    "    \"settings\": {"
    "      \"vnext\": [{"
    "        \"address\": \"10.0.0.1\","
    "        \"port\": 443,"
    "        \"users\": [{"
    "          \"id\": \"55555555-5555-4555-8555-555555555555\","
    "          \"encryption\": \"none\","
    "          \"flow\": \"xtls-rprx-vision\""
    "        }]"
    "      }]"
    "    },"
    "    \"streamSettings\": {"
    "      \"network\": \"tcp\","
    "      \"security\": \"reality\","
    "      \"realitySettings\": {"
    "        \"serverName\": \"solo.example\","
    "        \"publicKey\": \"" PBK "\","
    "        \"shortId\": \"ab\","
    "        \"fingerprint\": \"safari\""
    "      }"
    "    }"
    "  }, { \"protocol\": \"freedom\" }]"
    "}";

/* sing-box names every field differently and gates tls on its own flag. this is
   what the current panels and guis export */
static const char k_singbox_cfg[] =
    "{"
    "  \"outbounds\": ["
    "    {"
    "      \"type\": \"vless\","
    "      \"tag\": \"reality-node\","
    "      \"server\": \"sb.example\","
    "      \"server_port\": 8443,"
    "      \"uuid\": \"66666666-6666-4666-8666-666666666666\","
    "      \"flow\": \"xtls-rprx-vision\","
    "      \"packet_encoding\": \"xudp\","
    "      \"tls\": {"
    "        \"enabled\": true,"
    "        \"server_name\": \"sb.sni.example\","
    "        \"utls\": { \"enabled\": true, \"fingerprint\": \"chrome\" },"
    "        \"reality\": {"
    "          \"enabled\": true,"
    "          \"public_key\": \"" PBK "\","
    "          \"short_id\": \"beef\""
    "        }"
    "      }"
    "    },"
    "    {"
    "      \"type\": \"vless\","
    "      \"tag\": \"ws-node\","
    "      \"server\": \"ws.example\","
    "      \"server_port\": 443,"
    "      \"uuid\": \"77777777-7777-4777-8777-777777777777\","
    "      \"tls\": { \"enabled\": true, \"server_name\": \"ws.sni.example\" },"
    "      \"transport\": {"
    "        \"type\": \"ws\","
    "        \"path\": \"/tunnel\","
    "        \"headers\": { \"Host\": \"cdn.example\" }"
    "      }"
    "    },"
    "    {"
    "      \"type\": \"vless\","
    "      \"tag\": \"grpc-node\","
    "      \"server\": \"grpc.example\","
    "      \"server_port\": 2053,"
    "      \"uuid\": \"88888888-8888-4888-8888-888888888888\","
    "      \"tls\": { \"enabled\": true },"
    "      \"transport\": { \"type\": \"grpc\", \"service_name\": \"edge\" }"
    "    },"
    "    {"
    "      \"type\": \"trojan\","
    "      \"tag\": \"trojan-node\","
    "      \"server\": \"sbtrojan.example\","
    "      \"server_port\": 443,"
    "      \"password\": \"sbpw\","
    "      \"tls\": { \"enabled\": true, \"server_name\": \"sbtrojan.sni.example\" }"
    "    },"
    "    {"
    "      \"type\": \"shadowsocks\","
    "      \"tag\": \"ss-node\","
    "      \"server\": \"sbss.example\","
    "      \"server_port\": 8388,"
    "      \"method\": \"aes-256-gcm\","
    "      \"password\": \"sbsspw\""
    "    },"
    "    {"
    "      \"type\": \"hysteria2\","
    "      \"tag\": \"sb-hy2\","
    "      \"server\": \"sbhy.example\","
    "      \"server_port\": 44333,"
    "      \"password\": \"sbhypw\","
    "      \"obfs\": { \"type\": \"salamander\", \"password\": \"sbobfspw\" },"
    "      \"tls\": { \"enabled\": true, \"server_name\": \"sbhy.sni.example\" }"
    "    },"
    "    { \"type\": \"direct\", \"tag\": \"direct\" },"
    "    { \"type\": \"block\", \"tag\": \"block\" },"
    "    { \"type\": \"selector\", \"tag\": \"select\","
    "      \"outbounds\": [\"reality-node\", \"ws-node\"] }"
    "  ]"
    "}";

/* a tls block that is present but switched off is plain tcp, not tls */
static const char k_singbox_plain[] =
    "{ \"outbounds\": [{"
    "  \"type\": \"vless\","
    "  \"server\": \"plain.example\","
    "  \"server_port\": 80,"
    "  \"uuid\": \"99999999-9999-4999-8999-999999999999\","
    "  \"tls\": { \"enabled\": false, \"server_name\": \"ignored.example\" }"
    "}] }";

/* classic URI list must keep working; JSON https:// noise must not steal it */
static const char k_uri_list[] =
    "vless://11111111-1111-4111-8111-111111111111@1.1.1.1:443"
    "?security=reality&type=tcp&flow=xtls-rprx-vision&pbk=" PBK "&sid=00#uri\n";

int main(void) {
    vl_server_t srv[16];
    size_t n = 0;
    size_t i;

    ok("array parse ok",
       cfg_parse_subscription(k_array_cfg, strlen(k_array_cfg), srv, 16, &n) == CFG_OK);
    ok("array keeps grpc, trojan, shadowsocks and hysteria2", n == 7);

    ok("multi outbound keeps the feed name for ui candidate grouping",
       n >= 1 && strcmp(srv[0].remark, "Liberty Auto") == 0);
    ok("first is reality vision",
       n >= 1 && srv[0].security == VL_SEC_REALITY &&
       srv[0].net == VL_NET_TCP &&
       strcmp(srv[0].flow, "xtls-rprx-vision") == 0 &&
       strcmp(srv[0].sni, "cdn.example") == 0 &&
       strcmp(srv[0].fp, "firefox") == 0 &&
       strcmp(srv[0].sid, "abcd") == 0 &&
       strcmp(srv[0].pbk, PBK) == 0);

    ok("shortId array takes first",
       n >= 2 && strcmp(srv[1].host, "5.6.7.8") == 0 &&
       strcmp(srv[1].sid, "11") == 0 &&
       srv[1].port == 8443);

    ok("grpc fields",
       n >= 3 && srv[2].net == VL_NET_GRPC &&
       strcmp(srv[2].path, "/edge/Tun") == 0 &&
       strcmp(srv[2].mode, "grpc") == 0 &&
       strcmp(srv[2].sni, "g.example") == 0);

    ok("xhttp fields",
       n >= 4 && srv[3].net == VL_NET_XHTTP &&
       strcmp(srv[3].path, "/api/") == 0 &&
       strcmp(srv[3].mode, "packet-up") == 0 &&
       strcmp(srv[3].ws_host, "host.example") == 0 &&
       strcmp(srv[3].sni, "sni.example") == 0 &&
       strcmp(srv[3].remark, "XHTTP node") == 0);

    ok("xray trojan over ws",
       n >= 5 && srv[4].proto == VL_PROTO_TROJAN &&
       srv[4].security == VL_SEC_TLS && srv[4].net == VL_NET_WS &&
       strcmp(srv[4].host, "trojan.example") == 0 &&
       strcmp(srv[4].pass, "secretpw") == 0 &&
       strcmp(srv[4].sni, "trojan.sni.example") == 0 &&
       strcmp(srv[4].path, "/tws") == 0);

    ok("xray shadowsocks",
       n >= 6 && srv[5].proto == VL_PROTO_SHADOWSOCKS &&
       strcmp(srv[5].host, "ss.example") == 0 &&
       srv[5].port == 8388 &&
       strcmp(srv[5].pass, "sspass") == 0 &&
       strcmp(srv[5].encryption, "chacha20-ietf-poly1305") == 0);

    ok("xray hysteria2",
       n >= 7 && srv[6].proto == VL_PROTO_HYSTERIA2 &&
       strcmp(srv[6].host, "hy.example") == 0 &&
       srv[6].port == 44300 &&
       strcmp(srv[6].pass, "hypw") == 0 &&
       strcmp(srv[6].sni, "hy.sni.example") == 0 &&
       strcmp(srv[6].pin_sha256, "AA:BB") == 0 &&
       strcmp(srv[6].obfs, "salamander") == 0 &&
       strcmp(srv[6].obfs_password, "obfspw") == 0 &&
       strcmp(srv[6].port_hop, "44300,45000-46000") == 0);

    n = 0;
    ok("single object parse",
       cfg_parse_subscription(k_single_cfg, strlen(k_single_cfg), srv, 16, &n) == CFG_OK);
    ok("single yields 1", n == 1);
    ok("single keeps remarks",
       n == 1 && strcmp(srv[0].remark, "solo") == 0 &&
       strcmp(srv[0].host, "10.0.0.1") == 0);

    n = 0;
    ok("sing-box parse ok",
       cfg_parse_subscription(k_singbox_cfg, strlen(k_singbox_cfg), srv, 16, &n) == CFG_OK);
    ok("sing-box skips direct, block and selector", n == 6);
    ok("sing-box reality node",
       n >= 1 && strcmp(srv[0].host, "sb.example") == 0 &&
       srv[0].port == 8443 &&
       srv[0].security == VL_SEC_REALITY &&
       srv[0].net == VL_NET_TCP &&
       strcmp(srv[0].uuid, "66666666-6666-4666-8666-666666666666") == 0 &&
       strcmp(srv[0].flow, "xtls-rprx-vision") == 0 &&
       strcmp(srv[0].sni, "sb.sni.example") == 0 &&
       strcmp(srv[0].fp, "chrome") == 0 &&
       strcmp(srv[0].sid, "beef") == 0 &&
       strcmp(srv[0].pbk, PBK) == 0);
    ok("sing-box names the row after the tag",
       n >= 1 && strcmp(srv[0].remark, "reality-node") == 0);
    ok("sing-box websocket transport",
       n >= 2 && srv[1].net == VL_NET_WS &&
       srv[1].security == VL_SEC_TLS &&
       strcmp(srv[1].path, "/tunnel") == 0 &&
       strcmp(srv[1].ws_host, "cdn.example") == 0 &&
       strcmp(srv[1].sni, "ws.sni.example") == 0);
    ok("sing-box grpc transport and sni default",
       n >= 3 && srv[2].net == VL_NET_GRPC &&
       strcmp(srv[2].path, "/edge/Tun") == 0 &&
       strcmp(srv[2].sni, "grpc.example") == 0);
    ok("sing-box trojan",
       n >= 4 && srv[3].proto == VL_PROTO_TROJAN &&
       srv[3].security == VL_SEC_TLS &&
       strcmp(srv[3].host, "sbtrojan.example") == 0 &&
       strcmp(srv[3].pass, "sbpw") == 0 &&
       strcmp(srv[3].sni, "sbtrojan.sni.example") == 0);
    ok("sing-box shadowsocks",
       n >= 5 && srv[4].proto == VL_PROTO_SHADOWSOCKS &&
       strcmp(srv[4].host, "sbss.example") == 0 &&
       srv[4].port == 8388 &&
       strcmp(srv[4].pass, "sbsspw") == 0 &&
       strcmp(srv[4].encryption, "aes-256-gcm") == 0);
    ok("sing-box hysteria2",
       n >= 6 && srv[5].proto == VL_PROTO_HYSTERIA2 &&
       strcmp(srv[5].host, "sbhy.example") == 0 &&
       srv[5].port == 44333 &&
       strcmp(srv[5].pass, "sbhypw") == 0 &&
       strcmp(srv[5].sni, "sbhy.sni.example") == 0 &&
       strcmp(srv[5].obfs, "salamander") == 0 &&
       strcmp(srv[5].obfs_password, "sbobfspw") == 0);

    n = 0;
    ok("sing-box disabled tls is not tls",
       cfg_parse_subscription(k_singbox_plain, strlen(k_singbox_plain),
                              srv, 16, &n) == CFG_OK &&
       n == 1 && srv[0].security == VL_SEC_NONE && srv[0].sni[0] == '\0');

    n = 0;
    ok("uri list still works",
       cfg_parse_subscription(k_uri_list, strlen(k_uri_list), srv, 16, &n) == CFG_OK &&
       n == 1 && strcmp(srv[0].host, "1.1.1.1") == 0);

    /* optional live fixture when present */
    {
        FILE *f = fopen("/tmp/senko_sub_body.bin", "rb");
        if (f) {
            char *buf;
            long sz;
            if (fseek(f, 0, SEEK_END) == 0 && (sz = ftell(f)) > 0 && sz < 2 * 1024 * 1024) {
                rewind(f);
                buf = (char *)malloc((size_t)sz);
                if (buf && fread(buf, 1, (size_t)sz, f) == (size_t)sz) {
                    vl_server_t bulk[128];
                    size_t bn = 0;
                    ok("liberty body parses",
                       cfg_parse_subscription(buf, (size_t)sz, bulk, 128, &bn) == CFG_OK);
                    ok("liberty yields many vision nodes", bn >= 50);
                    fprintf(stderr, "liberty fixture: %zu servers\n", bn);
                    for (i = 0; i < bn && i < 3; ++i)
                        fprintf(stderr, "  [%zu] %s %s:%u sec=%d net=%d\n",
                                i, bulk[i].remark, bulk[i].host, bulk[i].port,
                                (int)bulk[i].security, (int)bulk[i].net);
                }
                free(buf);
            }
            fclose(f);
        }
    }

    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("all xray json subscription checks passed\n");
    return 0;
}
