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

    puts("go config tests passed");
    return 0;
}
