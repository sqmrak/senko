#define _DEFAULT_SOURCE
#include "senko_core_config.h"
#include "cJSON.h"
#include "geo.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cJSON *render(const char *link, char *buffer, size_t cap) {
    vl_server_t server;
    assert(cfg_parse_link(link, &server) == CFG_OK);
    assert(senko_core_config_render(&server, "203.0.113.7", "utun12", buffer, cap) == 0);
    cJSON *root = cJSON_Parse(buffer);
    assert(root != NULL);
    return root;
}

static cJSON *render_rules_geo(const char *link, ruleset_t *rules, const char *geo_dir,
                               char *buffer, size_t cap) {
    vl_server_t server;
    assert(cfg_parse_link(link, &server) == CFG_OK);
    assert(senko_core_config_render_rules(&server, "203.0.113.7", "utun12",
                                  rules, geo_dir, NULL, buffer, cap) == 0);
    cJSON *root = cJSON_Parse(buffer);
    assert(root != NULL);
    return root;
}

static cJSON *render_rules(const char *link, ruleset_t *rules,
                           char *buffer, size_t cap) {
    return render_rules_geo(link, rules, NULL, buffer, cap);
}

static void touch_file(const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    fclose(f);
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
    assert(ruleset_add_text(&rules, "block ip-cidr 192.0.2.0/24", 26, NULL) == RULES_OK);
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

    /* the flat vmess form infra/conf/vmess.go reads, with the shared stream */
    root = render(
        "vmess://11111111-1111-4111-8111-111111111111@example.com:443"
        "?encryption=chacha20-poly1305&security=tls&sni=cdn.example.com&type=ws&path=%2Fvm#vm",
        json, sizeof json);
    {
        cJSON *ob = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "outbounds"), 0);
        assert(strcmp(cJSON_GetObjectItem(ob, "protocol")->valuestring, "vmess") == 0);
        cJSON *settings = cJSON_GetObjectItem(ob, "settings");
        assert(strcmp(cJSON_GetObjectItem(settings, "address")->valuestring, "203.0.113.7") == 0);
        assert(cJSON_GetObjectItem(settings, "port")->valueint == 443);
        assert(strcmp(cJSON_GetObjectItem(settings, "id")->valuestring,
                      "11111111-1111-4111-8111-111111111111") == 0);
        assert(strcmp(cJSON_GetObjectItem(settings, "security")->valuestring,
                      "chacha20-poly1305") == 0);
        cJSON *stream = cJSON_GetObjectItem(ob, "streamSettings");
        assert(strcmp(cJSON_GetObjectItem(stream, "network")->valuestring, "ws") == 0);
        assert(strcmp(cJSON_GetObjectItem(stream, "security")->valuestring, "tls") == 0);
        assert(strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(stream, "wsSettings"), "path")->valuestring,
                      "/vm") == 0);
    }
    if (argc == 2 && strcmp(argv[1], "--print-vmess") == 0) {
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

    {
        /* full-domain rules, the catch-all of a direct fallback, and what the
           trimmed core cannot take */
        static ruleset_t fr;
        static const char link[] =
            "vless://11111111-1111-4111-8111-111111111111@example.com:443?type=tcp&security=tls";
        ruleset_init(&fr);
        assert(ruleset_add_text(&fr, "direct domain-full only.example", 31, NULL) == RULES_OK);
        assert(ruleset_add_text(&fr, "proxy domain-suffix via.example", 31, NULL) == RULES_OK);
        fr.fallback = RULE_ACTION_DIRECT;
        assert(senko_core_config_unsupported(&fr, NULL) == 0);
        root = render_rules(link, &fr, json, sizeof json);
        assert(strstr(json, "full:only.example") != NULL);
        const char *last_rule = strstr(json, "\"network\":\"tcp,udp\",\"outboundTag\":\"direct\"");
        assert(last_rule != NULL && last_rule > strstr(json, "domain:via.example"));
        cJSON_Delete(root);

        ruleset_init(&fr);
        assert(ruleset_add_text(&fr, "direct geosite ru", 17, NULL) == RULES_OK);
        assert(ruleset_add_text(&fr, "proxy geoip cloudflare", 22, NULL) == RULES_OK);
        assert(ruleset_add_text(&fr, "block app com.example.app", 25, NULL) == RULES_OK);
        assert(ruleset_add_text(&fr, "block domain-suffix ads.example", 31, NULL) == RULES_OK);
        fr.fallback = RULE_ACTION_DIRECT;
        assert(senko_core_config_unsupported(&fr, NULL) == 3);
        root = render_rules(link, &fr, json, sizeof json);
        assert(strstr(json, "ads.example") != NULL);
        assert(strstr(json, "geosite") == NULL && strstr(json, "geoip") == NULL &&
               strstr(json, "com.example.app") == NULL);
        assert(strstr(json, "\"network\":\"tcp,udp\"") == NULL);
        cJSON_Delete(root);
        assert(senko_core_config_unsupported(NULL, NULL) == 0);
    }
    {
        /* xray reads geo sets from the dats in the asset directory */
        char dir[] = "/tmp/senko-core-geo-XXXXXX";
        static ruleset_t gr;
        static const char link[] =
            "vless://11111111-1111-4111-8111-111111111111@example.com:443?type=tcp&security=tls";
        assert(mkdtemp(dir) != NULL);
        ruleset_init(&gr);
        gr.fallback = RULE_ACTION_DIRECT;
        assert(ruleset_add_text(&gr, "proxy geosite telegram", 22, NULL) == RULES_OK);
        assert(ruleset_add_text(&gr, "proxy geoip telegram", 20, NULL) == RULES_OK);
        assert(ruleset_add_text(&gr, "direct geoip private", 20, NULL) == RULES_OK);
        assert(ruleset_add_text(&gr, "direct geosite absent", 21, NULL) == RULES_OK);
        assert(ruleset_add_text(&gr, "block geosite google@ads", 24, NULL) == RULES_OK);

        geo_site_t *site = geo_site_new();
        assert(site && geo_site_add(site, GEO_SITE_SUFFIX, "t.me", 4) == GEO_OK);
        assert(geo_site_seal(site) == GEO_OK);
        char path[512];
        assert(geo_tag_path(dir, GEO_KIND_SITE, "telegram", path, sizeof path) == 0);
        assert(geo_site_save(site, path) == GEO_OK);
        assert(geo_tag_path(dir, GEO_KIND_SITE, "google@ads", path, sizeof path) == 0);
        assert(geo_site_save(site, path) == GEO_OK);
        geo_site_free(site);
        geo_site_t *none = geo_site_new();
        assert(none && geo_site_seal(none) == GEO_OK);
        assert(geo_tag_path(dir, GEO_KIND_SITE, "absent", path, sizeof path) == 0);
        assert(geo_site_save(none, path) == GEO_OK);
        geo_site_free(none);
        geo_ip_t *ip = geo_ip_new();
        const uint8_t net[4] = { 91, 108, 4, 0 };
        assert(ip && geo_ip_add(ip, net, 4, 22) == GEO_OK && geo_ip_seal(ip) == GEO_OK);
        assert(geo_tag_path(dir, GEO_KIND_IP, "telegram", path, sizeof path) == 0);
        assert(geo_ip_save(ip, path) == GEO_OK);
        geo_ip_free(ip);
        size_t missing = 0;
        assert(ruleset_bind_geo(&gr, dir, &missing) == RULES_OK && missing == 0);

        /* sets without their dats are lost: the utun backend knows them, xray would not */
        assert(senko_core_config_unsupported(&gr, dir) == 3);
        touch_file(dir, "geosite.dat");
        touch_file(dir, "geoip.dat");
        /* an empty set matches nothing on either backend, so it is not lost */
        assert(senko_core_config_unsupported(&gr, dir) == 0);
        assert(senko_core_config_unsupported(&gr, NULL) == 4);
        root = render_rules_geo(link, &gr, dir, json, sizeof json);
        assert(strstr(json, "\"geosite:telegram\"") != NULL);
        assert(strstr(json, "\"geoip:telegram\"") != NULL);
        assert(strstr(json, "\"geosite:google@ads\"") != NULL);
        assert(strstr(json, "geosite:absent") == NULL);
        assert(strstr(json, "geoip:private") == NULL && strstr(json, "\"10.0.0.0/8\"") != NULL);
        assert(strstr(json, "\"network\":\"tcp,udp\"") != NULL);
        cJSON_Delete(root);

        /* no directory for the core: geo rules stay out and the default stays proxy */
        root = render_rules(link, &gr, json, sizeof json);
        assert(strstr(json, "geosite:") == NULL && strstr(json, "\"network\":\"tcp,udp\"") == NULL);
        cJSON_Delete(root);
        ruleset_release_geo(&gr);
        char cleanup[600];
        snprintf(cleanup, sizeof cleanup, "rm -rf %s", dir);
        assert(system(cleanup) == 0);
    }
    puts("senko-core config tests passed");
    {
        /* a roots file makes the core verify against it instead of Security */
        static const char *links[] = {
            "vless://11111111-1111-4111-8111-111111111111@example.com:443?type=tcp&security=tls&sni=cdn.example.com",
            "hysteria2://sekret@example.com:443?sni=cdn.example.com",
        };
        for (size_t i = 0; i < sizeof links / sizeof links[0]; ++i) {
            vl_server_t server;
            char out[8192];
            assert(cfg_parse_link(links[i], &server) == CFG_OK);
            assert(senko_core_config_render_rules(&server, "203.0.113.7", "utun12", NULL, NULL,
                                          "/var/jb/usr/lib/senko/cacert.pem", out, sizeof out) == 0);
            cJSON *tree = cJSON_Parse(out);
            assert(tree != NULL);
            cJSON *tls = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetArrayItem(
                cJSON_GetObjectItem(tree, "outbounds"), 0), "streamSettings"), "tlsSettings");
            assert(cJSON_IsTrue(cJSON_GetObjectItem(tls, "disableSystemRoot")));
            cJSON *cert = cJSON_GetArrayItem(cJSON_GetObjectItem(tls, "certificates"), 0);
            assert(strcmp(cJSON_GetObjectItem(cert, "usage")->valuestring, "verify") == 0);
            assert(strcmp(cJSON_GetObjectItem(cert, "certificateFile")->valuestring,
                          "/var/jb/usr/lib/senko/cacert.pem") == 0);
            cJSON_Delete(tree);
            assert(senko_core_config_render_rules(&server, "203.0.113.7", "utun12", NULL, NULL,
                                          NULL, out, sizeof out) == 0);
            assert(strstr(out, "disableSystemRoot") == NULL);
        }
    }
    return 0;
}
