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
    "          \"tlsSettings\": { \"serverName\": \"g.example\" }"
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
    ok("array yields 3 (grpc skipped)", n == 3);

    ok("multi remark disambiguates host a",
       n >= 1 && strstr(srv[0].remark, "1.2.3.4") != NULL);
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

    ok("xhttp fields",
       n >= 3 && srv[2].net == VL_NET_XHTTP &&
       strcmp(srv[2].path, "/api/") == 0 &&
       strcmp(srv[2].mode, "packet-up") == 0 &&
       strcmp(srv[2].ws_host, "host.example") == 0 &&
       strcmp(srv[2].sni, "sni.example") == 0 &&
       strcmp(srv[2].remark, "XHTTP node") == 0);

    n = 0;
    ok("single object parse",
       cfg_parse_subscription(k_single_cfg, strlen(k_single_cfg), srv, 16, &n) == CFG_OK);
    ok("single yields 1", n == 1);
    ok("single keeps remarks",
       n == 1 && strcmp(srv[0].remark, "solo") == 0 &&
       strcmp(srv[0].host, "10.0.0.1") == 0);

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
