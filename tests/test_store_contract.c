#include "store.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
static void ok(const char *what, int cond) {
    if (cond) return;
    g_fail++;
    fprintf(stderr, "FAIL %s\n", what);
}

static cJSON *subscription_node(int index) {
    char host[64];
    snprintf(host, sizeof host, "node%d.example", index);
    cJSON *node = cJSON_CreateObject();
    cJSON_AddStringToObject(node, "protocol", "vless");
    cJSON *settings = cJSON_AddObjectToObject(node, "settings");
    cJSON_AddStringToObject(settings, "address", host);
    cJSON_AddNumberToObject(settings, "port", 443);
    cJSON_AddStringToObject(settings, "id", "11111111-1111-4111-8111-111111111111");
    cJSON_AddStringToObject(settings, "encryption", "none");
    cJSON *stream = cJSON_AddObjectToObject(node, "streamSettings");
    cJSON_AddStringToObject(stream, "network", index % 5 ? "tcp" : "kcp");
    cJSON_AddStringToObject(stream, "security", "none");
    return node;
}

static void test_balancer_subscription(void) {
    static store_t catalog;
    cJSON *profiles = cJSON_CreateArray();
    cJSON *automatic = cJSON_CreateObject();
    cJSON_AddItemToArray(profiles, automatic);
    cJSON_AddStringToObject(automatic, "remarks", "automatic");
    cJSON *outbounds = cJSON_AddArrayToObject(automatic, "outbounds");
    for (int i = 0; i < 300; ++i) {
        cJSON *node = subscription_node(i);
        cJSON_AddItemToArray(outbounds, node);
        cJSON_AddItemToArray(outbounds, cJSON_Duplicate(node, 1));
    }
    char padding[4097];
    memset(padding, 'x', sizeof padding - 1);
    padding[sizeof padding - 1] = '\0';
    for (int i = 0; i < 122; ++i) {
        char name[32];
        snprintf(name, sizeof name, "named %d", i);
        cJSON *profile = cJSON_CreateObject();
        cJSON_AddItemToArray(profiles, profile);
        cJSON_AddStringToObject(profile, "remarks", name);
        cJSON_AddStringToObject(profile, "padding", padding);
        cJSON *nodes = cJSON_AddArrayToObject(profile, "outbounds");
        cJSON_AddItemToArray(nodes, subscription_node(1));
    }
    char *body = cJSON_PrintUnformatted(profiles);
    cJSON_Delete(profiles);
    ok("large balancer fixture allocated", body != NULL);
    if (!body) return;
    ok("balancer fixture exceeds the former download limit", strlen(body) > 512 * 1024);
    store_init(&catalog);
    size_t sub = 0, added = 0;
    store_add_sub(&catalog, "panel", "https://panel.example/feed", &sub);
    ok("balancer subscription imports", store_refresh_sub(&catalog, sub, body,
       strlen(body), &added) == STORE_OK);
    ok("balancer and named profiles keep every distinct row", added == 422);
    size_t supported = 0, names = 0;
    for (size_t i = 0; i < catalog.n; ++i) {
        int seen = 0;
        supported += cfg_validate_server(&catalog.servers[i], NULL, 0) != 0;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(catalog.servers[i].remark, catalog.servers[j].remark)) seen = 1;
        if (!seen) ++names;
    }
    ok("all profiles after the automatic group survive", names == 123 && supported == 362);
    char *saved = malloc(512 * 1024);
    size_t length = 0;
    ok("large catalog save buffer allocated", saved != NULL);
    if (saved) {
        ok("large catalog serializes", store_serialize(&catalog, saved, 512 * 1024, &length) == STORE_OK);
        ok("large catalog survives a restart", store_deserialize(&catalog, saved, length) == STORE_OK &&
           catalog.n == 422 && !strcmp(catalog.servers[catalog.n - 1].remark, "named 121"));
    }
    free(saved);
    free(body);
}

int main(void) {
    test_balancer_subscription();
    {
        static store_t groups;
        const char *node = "vless://11111111-1111-4111-8111-111111111111@same.example:443?security=none#same";
        size_t first = 0, second = 0, added = 0;
        store_init(&groups);
        ok("first subscription", store_add_sub(&groups, "first", "https://first.example/feed", &first) == STORE_OK);
        ok("second subscription", store_add_sub(&groups, "second", "https://second.example/feed", &second) == STORE_OK);
        ok("first copy", store_refresh_sub(&groups, first, node, strlen(node), &added) == STORE_OK);
        ok("second copy", store_refresh_sub(&groups, second, node, strlen(node), &added) == STORE_OK);
        ok("select second copy", store_select(&groups, 1) == STORE_OK);
        ok("refresh selected subscription", store_refresh_sub(&groups, second, node, strlen(node), &added) == STORE_OK);
        ok("selection stays in its subscription", groups.selected == 1 && groups.group[groups.selected] == (int)second);
        ok("remove selected subscription", store_remove_sub(&groups, second) == STORE_OK);
        ok("another subscription does not inherit selection", groups.selected == -1);
    }
    {
        static store_t hy2;
        vl_server_t node;
        store_init(&hy2);
        ok("parse hysteria2 identity fixture", cfg_parse_link(
            "hysteria2://password@same.example:443?obfs=salamander&obfs-password=first", &node) == CFG_OK);
        ok("first hysteria2 profile", store_add_manual_server(&hy2, &node, NULL) == STORE_OK);
        snprintf(node.obfs_password, sizeof node.obfs_password, "second");
        ok("a different obfuscation password keeps its own server",
           store_add_manual_server(&hy2, &node, NULL) == STORE_OK && hy2.n == 2);
    }
    static store_t st;
    store_init(&st);

    size_t idx = 99;
    store_status_t r = store_add_manual(&st,
        "vless://11111111-1111-4111-8111-111111111111@example.com:443?security=reality&type=tcp&flow=xtls-rprx-vision#bad",
        &idx);
    ok("manual unsupported rejected", r == STORE_ERR_UNSUPPORTED);
    ok("manual unsupported not added", st.n == 0);

    r = store_add_manual(&st,
        "vless://11111111-1111-4111-8111-111111111111@example.com:443?security=tls&type=raw&flow=xtls-rprx-vision&serverName=edge.example#ok",
        &idx);
    ok("manual supported accepted", r == STORE_OK && st.n == 1 && idx == 0);

    size_t sub = 0;
    ok("add sub", store_add_sub(&st, "Sub", "https://sub.example/list", &sub) == STORE_OK);
    ok("replacement keeps subscription nodes and metadata slot",
       store_replace_sub(&st, sub, "Renamed", "https://sub.example/new-list",
                         "Authorization: Bearer test") == STORE_OK &&
       st.n == 1 && strcmp(st.subs[sub].name, "Renamed") == 0 &&
       strcmp(st.subs[sub].url, "https://sub.example/new-list") == 0 &&
       strcmp(st.subs[sub].header, "Authorization: Bearer test") == 0);
    ok("invalid subscription replacement leaves saved details intact",
       store_replace_sub(&st, sub, "", "https://other.example/list", "") == STORE_ERR_TOO_LONG &&
       strcmp(st.subs[sub].url, "https://sub.example/new-list") == 0);
    const char blob[] =
        "vless://22222222-2222-4222-8222-222222222222@ok.example:443?security=tls&sni=shared.example&type=tcp&flow=xtls-rprx-vision#ok\n"
        "vless://22222222-2222-4222-8222-222222222222@mirror.example:8443?security=tls&sni=shared.example&type=tcp&flow=xtls-rprx-vision#same-profile-other-route\n"
        "vless://22222222-2222-4222-8222-222222222222@distinct.example:443?security=tls&sni=distinct.example&type=tcp&flow=xtls-rprx-vision#different-profile\n"
        "vless://33333333-3333-4333-8333-333333333333@bad.example:443?security=reality&type=tcp&flow=xtls-rprx-vision#bad\n";
    size_t added = 0;
    ok("refresh accepts a feed with an unusable node",
       store_refresh_sub(&st, sub, blob, sizeof blob - 1, &added) == STORE_OK);
    ok("subscription keeps mirrored routes and the unusable node", added == 4);
    ok("store count after refresh", st.n == 5);
    ok("unusable node stays as a placeholder row",
       st.servers[4].proto == VL_PROTO_UNSUPPORTED &&
       strcmp(st.servers[4].remark, "bad") == 0 &&
       strcmp(st.servers[4].host, "bad.example") == 0 && st.servers[4].port == 443 &&
       st.servers[4].unsupported[0] && !cfg_validate_server(&st.servers[4], NULL, 0));
    {
        char link[256];
        ok("placeholder has no share link", store_link_at(&st, 4, link, sizeof link) != 0);
    }

    ok("select subscription server", store_select(&st, 1) == STORE_OK);
    const char renamed[] =
        "vless://22222222-2222-4222-8222-222222222222@ok.example:443?security=tls&sni=shared.example&type=tcp&flow=xtls-rprx-vision#renamed\n";
    ok("refresh with renamed server", 
       store_refresh_sub(&st, sub, renamed, sizeof renamed - 1, &added) == STORE_OK);
    ok("refresh keeps selected endpoint", st.selected >= 0 &&
       strcmp(st.servers[st.selected].host, "ok.example") == 0 &&
       strcmp(st.servers[st.selected].remark, "renamed") == 0);

    size_t manual_two = 99;
    ok("add second manual",
       store_add_manual(&st,
           "vless://44444444-4444-4444-8444-444444444444@manual.example:443?security=none&type=tcp#manual2",
           &manual_two) == STORE_OK);
    ok("invalid replacement leaves manual server intact",
       store_replace_manual(&st, 0,
           "vless://not-a-link") == STORE_ERR_PARSE &&
       strcmp(st.servers[0].host, "example.com") == 0);
    ok("replacement keeps the manual server slot",
       store_replace_manual(&st, 0,
           "vless://55555555-5555-4555-8555-555555555555@edited.example:443?security=none&type=tcp#edited") == STORE_OK &&
       st.n == 3 && st.group[0] == STORE_GROUP_MANUAL &&
       strcmp(st.servers[0].host, "edited.example") == 0);
    ok("replacement rejects another server without changing the saved entry",
       store_replace_manual(&st, 0,
           "vless://44444444-4444-4444-8444-444444444444@manual.example:443?security=none&type=tcp#manual2") == STORE_ERR_EXISTS &&
       strcmp(st.servers[0].host, "edited.example") == 0);
    ok("section order starts with manual",
       store_section_count(&st) == 2 &&
       store_section_at(&st, 0) == STORE_GROUP_MANUAL &&
       store_section_at(&st, 1) == (int)sub);
    ok("move subscription section",
       store_move_section(&st, (int)sub, 0) == STORE_OK &&
       store_section_at(&st, 0) == (int)sub &&
       store_section_at(&st, 1) == STORE_GROUP_MANUAL);
    ok("move manual server",
       store_move_manual(&st, manual_two, 0) == STORE_OK &&
       strcmp(st.servers[0].host, "manual.example") == 0);
    ok("move keeps selected endpoint", st.selected >= 0 &&
       strcmp(st.servers[st.selected].host, "ok.example") == 0);

    store_set_sub_expire(&st, sub, 1893456000ULL);
    store_set_sub_meta(&st, sub, 1024ULL * 1024ULL, 2ULL * 1024ULL * 1024ULL,
                       10ULL * 1024ULL * 1024ULL, "monthly plan",
                       "https://support.example/plan");
    size_t rule_index = 99;
    ok("add stored rule", store_add_rule(&st,
       "direct domain-suffix Example.COM", 32, &rule_index) == STORE_OK &&
       rule_index == 0);
    ok("add stored block", store_add_rule(&st,
       "block ip-cidr 192.0.2.129/24", 28, NULL) == STORE_OK);
    char saved[8192];
    size_t saved_len = 0;
    ok("serialize order and expiry",
       store_serialize(&st, saved, sizeof saved, &saved_len) == STORE_OK &&
       strstr(saved, "ORDER ") != NULL && strstr(saved, "SUBMETA ") != NULL &&
       strstr(saved, "SUBINFO ") != NULL &&
       strstr(saved, "SET rule direct domain-suffix example.com\n") != NULL &&
       strstr(saved, "SET rule block ip-cidr 192.0.2.0/24\n") != NULL);
    static store_t restored;
    ok("deserialize order and expiry",
       store_deserialize(&restored, saved, saved_len) == STORE_OK &&
       restored.subs[sub].expire == 1893456000ULL &&
       restored.subs[sub].upload == 1024ULL * 1024ULL &&
       restored.subs[sub].download == 2ULL * 1024ULL * 1024ULL &&
       restored.subs[sub].total == 10ULL * 1024ULL * 1024ULL &&
       restored.rules.count == 2 &&
       ruleset_match_domain(&restored.rules, "www.example.com", NULL) ==
           RULE_ACTION_DIRECT &&
       ruleset_match_ip(&restored.rules, "192.0.2.7", NULL) == RULE_ACTION_BLOCK &&
       strcmp(restored.subs[sub].description, "monthly plan") == 0 &&
       strcmp(restored.subs[sub].support_url, "https://support.example/plan") == 0 &&
       store_section_at(&restored, 0) == (int)sub &&
       store_section_at(&restored, 1) == STORE_GROUP_MANUAL);

    const size_t before_empty = st.n;
    const char empty[] = "not a server\n";
    added = 99;
    ok("empty refresh rejected", store_refresh_sub(&st, sub, empty, sizeof empty - 1, &added) == STORE_ERR_PARSE);
    ok("empty refresh preserves servers", st.n == before_empty);

/* the one button that empties the manual group has to leave subscription
   servers and their group ids intact */
    {
        size_t sub_before = 0;
        for (size_t i = 0; i < st.n; ++i)
            if (st.group[i] != STORE_GROUP_MANUAL) sub_before++;
        size_t removed = 0;
        ok("clear manual", store_clear_manual(&st, &removed) == STORE_OK);
        ok("clear manual removed something", removed > 0);
        ok("clear manual kept the subscription servers", st.n == sub_before);
        int any_manual = 0;
        for (size_t i = 0; i < st.n; ++i)
            if (st.group[i] == STORE_GROUP_MANUAL) any_manual = 1;
        ok("no manual server survives", !any_manual);
        ok("selection stays inside the list",
           st.selected < 0 || (size_t)st.selected < st.n);
        removed = 99;
        ok("clearing twice is not an error",
           store_clear_manual(&st, &removed) == STORE_OK && removed == 0);
    }

/* the on-disk config round-trips every server through build_link() and back
   through cfg_parse_link() on the next launch. a field build_link() forgets
   to serialize silently reverts to its default every restart: allowInsecure
   was one such field, and a trojan/vless node behind a self-signed cert
   would lose it and start failing its tls handshake after a respring. */
    {
        static store_t rt;
        store_init(&rt);
        size_t ridx = 0;
        ok("insecure trojan link accepted",
           store_add_manual(&rt,
               "trojan://p@insecure.example:443?security=tls&type=ws"
               "&sni=front.example&fp=chrome&path=%2Ftrojan&host=ws.example"
               "&allowInsecure=1#Insecure",
               &ridx) == STORE_OK);

        char relink[2048];
        ok("trojan link rebuilds", store_link_at(&rt, ridx, relink, sizeof relink) == 0);

        vl_server_t reparsed;
        memset(&reparsed, 0, sizeof reparsed);
        ok("rebuilt trojan link reparses", cfg_parse_link(relink, &reparsed) == CFG_OK);
        ok("allowInsecure survives the round trip", reparsed.insecure == 1);
        ok("fingerprint survives the round trip", strcmp(reparsed.fp, "chrome") == 0);
        ok("ws path survives the round trip", strcmp(reparsed.path, "/trojan") == 0);
        ok("ws host survives the round trip", strcmp(reparsed.ws_host, "ws.example") == 0);

        size_t vidx = 0;
        ok("insecure vless link accepted",
           store_add_manual(&rt,
               "vless://11111111-1111-4111-8111-111111111111@insecure.example:443"
               "?security=tls&type=tcp&flow=xtls-rprx-vision&allowInsecure=1#InsecureV",
               &vidx) == STORE_OK);
        ok("vless link rebuilds", store_link_at(&rt, vidx, relink, sizeof relink) == 0);
        memset(&reparsed, 0, sizeof reparsed);
        ok("rebuilt vless link reparses", cfg_parse_link(relink, &reparsed) == CFG_OK);
        ok("vless allowInsecure survives the round trip", reparsed.insecure == 1);
    }

/* the panel's suggested title must land as the subscription's name, but only
   while nobody has renamed it away from the url-derived default */
    {
        static store_t ts;
        store_init(&ts);
        size_t t = 0;
        ok("add titled sub",
           store_add_sub(&ts, "sub.example", "https://sub.example/list", &t) == STORE_OK);
        ok("title adopted while name is still the default",
           store_set_sub_title(&ts, t, "My Plan", "sub.example") == STORE_OK &&
           strcmp(ts.subs[t].name, "My Plan") == 0);
        ok("title not readopted once renamed by hand",
           store_replace_sub(&ts, t, "Chosen Name", "https://sub.example/list", "") == STORE_OK &&
           store_set_sub_title(&ts, t, "Panel Rename", "sub.example") == STORE_OK &&
           strcmp(ts.subs[t].name, "Chosen Name") == 0);
    }

    {
/* an xray json feed lists its single-server profiles again inside its
   auto-select profile. each name is its own row, so a node shared by two rows
   stays in both, and only a repeat inside one row collapses */
#define NODE(host) "{\"protocol\":\"vless\",\"settings\":{\"vnext\":[{\"address\":\"" host \
        "\",\"port\":443,\"users\":[{\"id\":\"44444444-4444-4444-8444-444444444444\"," \
        "\"flow\":\"xtls-rprx-vision\",\"encryption\":\"none\"}]}]},\"streamSettings\":" \
        "{\"network\":\"tcp\",\"security\":\"tls\",\"tlsSettings\":{\"serverName\":\"" host "\"}}}"
        static const char feed[] =
            "[{\"remarks\":\"Auto\",\"outbounds\":[" NODE("n1.example") "," NODE("n2.example") ","
            NODE("n1.example") "]},"
            "{\"remarks\":\"N1\",\"outbounds\":[" NODE("n1.example") "]},"
            "{\"remarks\":\"N2\",\"outbounds\":[" NODE("n2.example") "]}]";
#undef NODE
        static store_t js;
        size_t jsub = 0, jadded = 0;
        store_init(&js);
        ok("json sub", store_add_sub(&js, "Json", "https://json.example/sub", &jsub) == STORE_OK);
        ok("json refresh", store_refresh_sub(&js, jsub, feed, sizeof feed - 1, &jadded) == STORE_OK);
        ok("a node shared by two named rows stays in both, a repeat in one row does not",
           jadded == 4 && js.n == 4);
        int n2_row = -1;
        for (size_t i = 0; i < js.n; ++i)
            if (strcmp(js.servers[i].remark, "N2") == 0) n2_row = (int)i;
        ok("the single-server profile keeps its row", n2_row >= 0 &&
           strcmp(js.servers[n2_row].host, "n2.example") == 0);
        ok("select the single-server row", store_select(&js, n2_row) == STORE_OK);
        ok("json refresh again", store_refresh_sub(&js, jsub, feed, sizeof feed - 1, &jadded) == STORE_OK);
        ok("a refresh keeps the selection in its own row, not the auto row sharing its node",
           js.selected >= 0 && strcmp(js.servers[js.selected].remark, "N2") == 0);
    }

    {
/* a feed keeps every entry: what senko cannot run becomes a red placeholder
   row that survives a restart and moves pings with the other rows */
        static store_t ps, back;
        static char saved_ps[16384];
        size_t psub = 0, padded = 0, plen = 0;
        int moved[STORE_MAX_SERVERS];
        const char feed[] =
            "vless://44444444-4444-4444-8444-444444444444@a.example:443?security=tls&type=tcp#A\n"
            /* vmess {"ps":"VM node","add":"vm.example","port":"8443","id":...,"net":"kcp"} */
            "vmess://eyJwcyI6IlZNIG5vZGUiLCJhZGQiOiJ2bS5leGFtcGxlIiwicG9ydCI6Ijg0NDMiLCJpZCI6IjQ0NDQ0NDQ0LTQ0NDQtNDQ0NC04NDQ0LTQ0NDQ0NDQ0NDQ0NCIsIm5ldCI6ImtjcCJ9\n"
            "tuic://uuid:pw@[2001:db8::1]:9443?alpn=h3#T%20one\n"
            "vless://44444444-4444-4444-8444-444444444444@b.example:443?security=tls&type=tcp#B\n";
        store_init(&ps);
        ok("placeholder sub", store_add_sub(&ps, "P", "https://p.example/sub", &psub) == STORE_OK);
        ok("manual row before the subscription",
           store_add_manual(&ps, "vless://55555555-5555-4555-8555-555555555555@m.example:443?security=tls&type=tcp#M", NULL) == STORE_OK);
        cfg_import_stats_t stats;
        ok("refresh keeps unsupported links",
           store_refresh_sub_ex(&ps, psub, feed, sizeof feed - 1, &padded, &stats, moved) == STORE_OK &&
           padded == 4 && ps.n == 5 && stats.skipped == 2);
        ok("vmess placeholder reads its json body",
           ps.servers[2].proto == VL_PROTO_UNSUPPORTED &&
           strcmp(ps.servers[2].remark, "VM node") == 0 &&
           strcmp(ps.servers[2].host, "vm.example") == 0 && ps.servers[2].port == 8443 &&
           strcmp(ps.servers[2].unsupported, "kcp transport") == 0);
        ok("tuic placeholder reads the authority and fragment",
           ps.servers[3].proto == VL_PROTO_UNSUPPORTED &&
           strcmp(ps.servers[3].remark, "T one") == 0 &&
           strcmp(ps.servers[3].host, "2001:db8::1") == 0 && ps.servers[3].port == 9443);
        ok("placeholders keep the feed order",
           strcmp(ps.servers[1].remark, "A") == 0 && strcmp(ps.servers[4].remark, "B") == 0);
        ok("placeholder survives serialize",
           store_serialize(&ps, saved_ps, sizeof saved_ps, &plen) == STORE_OK &&
           strstr(saved_ps, "USRV ") != NULL &&
           store_deserialize(&back, saved_ps, plen) == STORE_OK && back.n == 5 &&
           back.servers[3].proto == VL_PROTO_UNSUPPORTED && back.group[3] == (int)psub &&
           strcmp(back.servers[3].remark, "T one") == 0 &&
           strcmp(back.servers[3].host, "2001:db8::1") == 0 &&
           back.servers[3].port == 9443 &&
           strcmp(back.servers[3].unsupported, ps.servers[3].unsupported) == 0);

        const char shorter[] =
            "vless://44444444-4444-4444-8444-444444444444@b.example:443?security=tls&type=tcp#B\n";
        ok("second refresh", store_refresh_sub_ex(&ps, psub, shorter, sizeof shorter - 1,
                                                  &padded, &stats, moved) == STORE_OK &&
           ps.n == 2);
        ok("moved keeps the manual row and follows B to its new index",
           moved[0] == 0 && moved[1] == -1 && moved[2] == -1 && moved[3] == -1 &&
           moved[4] == 1 && strcmp(ps.servers[1].remark, "B") == 0);

        const char only_vmess[] =
            "vmess://eyJwcyI6IlZNIG5vZGUiLCJhZGQiOiJ2bS5leGFtcGxlIiwicG9ydCI6Ijg0NDMiLCJpZCI6IjQ0NDQ0NDQ0LTQ0NDQtNDQ0NC04NDQ0LTQ0NDQ0NDQ0NDQ0NCIsIm5ldCI6ImtjcCJ9\n";
        size_t madded = 0;
        ok("manual import still skips what it cannot run",
           cfg_parse_subscription(only_vmess, sizeof only_vmess - 1, back.servers, 4, &madded) == CFG_OK &&
           madded == 0);

        const char xray[] =
            "[{\"remarks\":\"VM\",\"outbounds\":[{\"protocol\":\"wireguard\",\"settings\":"
            "{\"vnext\":[{\"address\":\"x.example\",\"port\":443}]}}]}]";
        ok("xray json keeps an unknown outbound",
           store_refresh_sub_ex(&ps, psub, xray, sizeof xray - 1, &padded, &stats, NULL) == STORE_OK &&
           padded == 1 && ps.servers[ps.n - 1].proto == VL_PROTO_UNSUPPORTED &&
           strcmp(ps.servers[ps.n - 1].remark, "VM") == 0 &&
           strcmp(ps.servers[ps.n - 1].host, "x.example") == 0 &&
           ps.servers[ps.n - 1].port == 443);
    }


    {
        /* a subscription's happ routing profile: stored, saved, rebuilt at connect */
        static store_t rs, back;
        static char saved[256 * 1024];
        size_t sub_a = 0, sub_b = 0, saved_len = 0;
        routing_info_t info;
        const char profile[] = "{\"Name\":\"P\",\"GlobalProxy\":\"false\","
            "\"DirectSites\":[\"domain:direct.test\",\"geosite:ru\"],\"BlockIp\":[\"10.1.0.0/16\"],"
            "\"ProxySites\":[\"domain:via.test\"]}";
        store_init(&rs);
        ok("routing starts off", rs.routing_active == -1);
        store_add_sub(&rs, "A", "https://a.example/x", &sub_a);
        store_add_sub(&rs, "B", "https://b.example/x", &sub_b);
        ok("profile stored", store_set_sub_routing(&rs, sub_a, profile, strlen(profile)) == STORE_OK &&
           strcmp(store_sub_routing(&rs, sub_a), profile) == 0 && store_sub_routing(&rs, sub_b)[0] == '\0');
        ok("bad profile refused", store_set_sub_routing(&rs, sub_b, "{\"x\":1}", 7) == STORE_ERR_PARSE &&
           store_set_sub_routing(&rs, sub_b, "nope", 4) == STORE_ERR_PARSE);
        ok("activate needs a profile", store_set_routing_active(&rs, (int)sub_b) == STORE_ERR_RANGE &&
           store_set_routing_active(&rs, 31) == STORE_ERR_RANGE &&
           store_set_routing_active(&rs, (int)sub_a) == STORE_OK && rs.routing_active == (int)sub_a);
        ok("user rule", store_add_rule(&rs, "block domain-suffix user.test", 29, NULL) == STORE_OK);

        ok("apply", store_apply_routing(&rs, &info) == STORE_OK && info.rules == 4 &&
           rs.rules.count == 5 && rs.rules.fallback == RULE_ACTION_DIRECT);
        ok("profile rules tagged", rs.rules.entries[0].source == 0 && rs.rules.entries[1].source == (uint8_t)(sub_a + 1));
        ok("apply twice does not stack", store_apply_routing(&rs, NULL) == STORE_OK && rs.rules.count == 5);

        ok("serialize skips profile rules", store_serialize(&rs, saved, sizeof saved, &saved_len) == STORE_OK &&
           strstr(saved, "direct.test") == NULL && strstr(saved, "SET rule block domain-suffix user.test") != NULL &&
           strstr(saved, "SUBROUTING 0 ") != NULL && strstr(saved, "ROUTINGACTIVE 0") != NULL);
        ok("deserialize", store_deserialize(&back, saved, saved_len) == STORE_OK &&
           strcmp(store_sub_routing(&back, sub_a), profile) == 0 && back.routing_active == (int)sub_a &&
           back.rules.count == 1);
        ok("rebuilt after restart", store_apply_routing(&back, &info) == STORE_OK && back.rules.count == 5 &&
           back.rules.fallback == RULE_ACTION_DIRECT);

        ok("switch off restores the fallback", store_set_routing_active(&rs, -1) == STORE_OK &&
           store_apply_routing(&rs, &info) == STORE_OK && rs.rules.count == 1 &&
           rs.rules.fallback == RULE_ACTION_PROXY && info.rules == 0);
        store_set_routing_active(&rs, (int)sub_a);
        store_apply_routing(&rs, NULL);
        ok("removing the subscription drops its profile", store_remove_sub(&rs, sub_a) == STORE_OK &&
           rs.routing_active == -1 && store_sub_routing(&rs, sub_a)[0] == '\0');
        ok("rules of a removed subscription go at the next apply",
           store_apply_routing(&rs, NULL) == STORE_OK && rs.rules.count == 1);
        store_add_sub(&rs, "A", "https://a.example/x", &sub_a);
        store_set_sub_routing(&rs, sub_a, profile, strlen(profile));
        store_set_routing_active(&rs, (int)sub_a);
        ok("blank clears and switches off", store_set_sub_routing(&rs, sub_a, "", 0) == STORE_OK &&
           rs.routing_active == -1 && store_sub_routing(&rs, sub_a)[0] == '\0');
    }

    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("all store contract checks passed\n");
    return 0;
}
