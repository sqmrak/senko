#include "store.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static void ok(const char *what, int cond) {
    if (cond) return;
    g_fail++;
    fprintf(stderr, "FAIL %s\n", what);
}

int main(void) {
    store_t st;
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
    ok("refresh filters unsupported",
       store_refresh_sub(&st, sub, blob, sizeof blob - 1, &added) == STORE_OK);
    ok("subscription keeps mirrored routes as separate fallback endpoints", added == 3);
    ok("store count after filter", st.n == 4);

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
       "block ip-cidr 192.0.2.129/24", 30, NULL) == STORE_OK);
    char saved[8192];
    size_t saved_len = 0;
    ok("serialize order and expiry",
       store_serialize(&st, saved, sizeof saved, &saved_len) == STORE_OK &&
       strstr(saved, "ORDER ") != NULL && strstr(saved, "SUBMETA ") != NULL &&
       strstr(saved, "SUBINFO ") != NULL &&
       strstr(saved, "SET rule direct domain-suffix example.com\n") != NULL &&
       strstr(saved, "SET rule block ip-cidr 192.0.2.0/24\n") != NULL);
    store_t restored;
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
        store_t rt;
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
        store_t ts;
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

    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("all store contract checks passed\n");
    return 0;
}
