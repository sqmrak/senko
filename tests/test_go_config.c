#include "go_config.h"
#include "cJSON.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static cJSON *render(const char *link, char *buffer, size_t cap) {
    vl_server_t server;
    assert(cfg_parse_link(link, &server) == CFG_OK);
    assert(go_config_render(&server, "203.0.113.7", "utun12", buffer, cap) == 0);
    cJSON *root = cJSON_Parse(buffer);
    assert(root != NULL);
    return root;
}

static cJSON *render_rules(const char *link, ruleset_t *rules,
                           char *buffer, size_t cap) {
    vl_server_t server;
    assert(cfg_parse_link(link, &server) == CFG_OK);
    assert(go_config_render_rules(&server, "203.0.113.7", "utun12",
                                  rules, buffer, cap) == 0);
    cJSON *root = cJSON_Parse(buffer);
    assert(root != NULL);
    return root;
}

int main(int argc, char **argv) {
    char json[8192];
    cJSON *root = render(
        "vless://11111111-1111-4111-8111-111111111111@example.com:443"
        "?type=tcp&security=reality&sni=cdn.example.com&fp=chrome"
        "&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA&sid=0123456789abcdef"
        "&flow=xtls-rprx-vision#node", json, sizeof json);
    assert(strstr(json, "\"address\":\"203.0.113.7\"") != NULL);
    assert(strstr(json, "\"serverName\":\"cdn.example.com\"") != NULL);
    assert(strstr(json, "\"publicKey\":\"AAAAAAAA") != NULL);
    assert(strstr(json, "\"protocol\":\"tun\"") != NULL);
    if (argc == 2 && strcmp(argv[1], "--print-vless") == 0) {
        puts(json);
        cJSON_Delete(root);
        return 0;
    }
    cJSON_Delete(root);

    ruleset_t rules;
    ruleset_init(&rules);
    assert(ruleset_add_text(&rules, "proxy domain-suffix shared.example", 34, NULL) == RULES_OK);
    assert(ruleset_add_text(&rules, "direct domain-keyword video", 27, NULL) == RULES_OK);
    assert(ruleset_add_text(&rules, "block domain-suffix ads.example", 31, NULL) == RULES_OK);
    assert(ruleset_add_text(&rules, "block ip-cidr 192.0.2.0/24", 28, NULL) == RULES_OK);
    root = render_rules(
        "vless://11111111-1111-4111-8111-111111111111@example.com:443"
        "?type=tcp&security=tls", &rules, json, sizeof json);
    assert(strstr(json, "\"protocol\":\"freedom\"") != NULL);
    assert(strstr(json, "\"protocol\":\"blackhole\"") != NULL);
    assert(strstr(json, "\"domainStrategy\":\"IPIfNonMatch\"") != NULL);
    const char *block = strstr(json, "domain:ads.example");
    const char *direct = strstr(json, "keyword:video");
    const char *proxy = strstr(json, "domain:shared.example");
    assert(block && direct && proxy && block < direct && direct < proxy);
    assert(strstr(json, "\"ip\":[\"192.0.2.0/24\"]") != NULL);
    if (argc == 2 && strcmp(argv[1], "--print-rules") == 0) {
        puts(json);
        cJSON_Delete(root);
        return 0;
    }
    cJSON_Delete(root);

    root = render(
        "vless://11111111-1111-4111-8111-111111111111@example.com:443"
        "?type=grpc&security=tls&sni=cdn.example.com&serviceName=senko", json, sizeof json);
    assert(strstr(json, "\"grpcSettings\":{\"serviceName\":\"senko\"}") != NULL);
    if (argc == 2 && strcmp(argv[1], "--print-grpc") == 0) {
        puts(json);
        cJSON_Delete(root);
        return 0;
    }
    cJSON_Delete(root);

    root = render("socks5://user:p%40ss@example.com:1080#proxy", json, sizeof json);
    assert(strstr(json, "\"protocol\":\"socks\"") != NULL);
    assert(strstr(json, "\"pass\":\"p@ss\"") != NULL);
    if (argc == 2 && strcmp(argv[1], "--print-socks") == 0) {
        puts(json);
        cJSON_Delete(root);
        return 0;
    }
    cJSON_Delete(root);

    root = render(
        "vless://11111111-1111-4111-8111-111111111111@example.com:443"
        "?type=ws&security=tls&sni=cdn.example.com&host=edge.example.com&path=%2Fws", json, sizeof json);
    assert(strstr(json, "\"wsSettings\"") != NULL);
    if (argc == 2 && strcmp(argv[1], "--print-ws") == 0) {
        puts(json); cJSON_Delete(root); return 0;
    }
    cJSON_Delete(root);

    root = render(
        "vless://11111111-1111-4111-8111-111111111111@example.com:443"
        "?type=xhttp&security=reality&sni=cdn.example.com&fp=chrome"
        "&pbk=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA&sid=0123456789abcdef"
        "&path=%2Fsplit&mode=stream-up", json, sizeof json);
    assert(strstr(json, "\"xhttpSettings\"") != NULL);
    if (argc == 2 && strcmp(argv[1], "--print-xhttp") == 0) {
        puts(json); cJSON_Delete(root); return 0;
    }
    cJSON_Delete(root);

    root = render("https://user:secret@example.com:8443#https-proxy", json, sizeof json);
    assert(strstr(json, "\"protocol\":\"http\"") != NULL);
    assert(strstr(json, "\"streamSettings\":{\"security\":\"tls\"") != NULL);
    if (argc == 2 && strcmp(argv[1], "--print-https") == 0) {
        puts(json); cJSON_Delete(root); return 0;
    }
    cJSON_Delete(root);

    root = render(
        "hysteria2://sekret@example.com:443?sni=cdn.example.com"
        "&pinSHA256=AA:BB:CC#hy", json, sizeof json);
    {
        cJSON *outbounds = cJSON_GetObjectItem(root, "outbounds");
        cJSON *ob = cJSON_GetArrayItem(outbounds, 0);
        assert(strcmp(cJSON_GetObjectItem(ob, "protocol")->valuestring, "hysteria") == 0);
        cJSON *settings = cJSON_GetObjectItem(ob, "settings");
        assert(cJSON_GetObjectItem(settings, "version")->valueint == 2);
        assert(strcmp(cJSON_GetObjectItem(settings, "address")->valuestring,
                      "203.0.113.7") == 0);
        cJSON *stream = cJSON_GetObjectItem(ob, "streamSettings");
        assert(strcmp(cJSON_GetObjectItem(stream, "network")->valuestring, "hysteria") == 0);
        cJSON *tls = cJSON_GetObjectItem(stream, "tlsSettings");
        assert(strcmp(cJSON_GetObjectItem(tls, "serverName")->valuestring,
                      "cdn.example.com") == 0);
        assert(strcmp(cJSON_GetObjectItem(tls, "pinnedPeerCertSha256")->valuestring,
                      "AA:BB:CC") == 0);
        cJSON *hy = cJSON_GetObjectItem(stream, "hysteriaSettings");
        assert(cJSON_GetObjectItem(hy, "version")->valueint == 2);
        assert(strcmp(cJSON_GetObjectItem(hy, "auth")->valuestring, "sekret") == 0);
    }
    if (argc == 2 && strcmp(argv[1], "--print-hysteria2") == 0) {
        puts(json); cJSON_Delete(root); return 0;
    }
    cJSON_Delete(root);

    root = render(
        "hysteria2://sekret@example.com:123,5000-6000?sni=cdn.example.com"
        "&obfs=salamander&obfs-password=maskpw#hy2", json, sizeof json);
    {
        cJSON *outbounds = cJSON_GetObjectItem(root, "outbounds");
        cJSON *ob = cJSON_GetArrayItem(outbounds, 0);
        cJSON *stream = cJSON_GetObjectItem(ob, "streamSettings");
        cJSON *fm = cJSON_GetObjectItem(stream, "finalmask");
        assert(fm != NULL);
        cJSON *udp = cJSON_GetObjectItem(fm, "udp");
        cJSON *mask0 = cJSON_GetArrayItem(udp, 0);
        assert(strcmp(cJSON_GetObjectItem(mask0, "type")->valuestring, "salamander") == 0);
        cJSON *msettings = cJSON_GetObjectItem(mask0, "settings");
        assert(strcmp(cJSON_GetObjectItem(msettings, "password")->valuestring,
                      "maskpw") == 0);
        cJSON *qp = cJSON_GetObjectItem(fm, "quicParams");
        cJSON *hop = cJSON_GetObjectItem(qp, "udpHop");
        assert(strcmp(cJSON_GetObjectItem(hop, "ports")->valuestring,
                      "123,5000-6000") == 0);
    }
    if (argc == 2 && strcmp(argv[1], "--print-hysteria2-finalmask") == 0) {
        puts(json); cJSON_Delete(root); return 0;
    }
    cJSON_Delete(root);

    puts("go config tests passed");
    return 0;
}
