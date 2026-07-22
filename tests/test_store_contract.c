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
    const char blob[] =
        "vless://22222222-2222-4222-8222-222222222222@ok.example:443?security=tls&type=tcp&flow=xtls-rprx-vision#ok\n"
        "vless://33333333-3333-4333-8333-333333333333@bad.example:443?security=reality&type=tcp&flow=xtls-rprx-vision#bad\n";
    size_t added = 0;
    ok("refresh filters unsupported",
       store_refresh_sub(&st, sub, blob, sizeof blob - 1, &added) == STORE_OK);
    ok("only supported subscription server added", added == 1);
    ok("store count after filter", st.n == 2);

    ok("select subscription server", store_select(&st, 1) == STORE_OK);
    const char renamed[] =
        "vless://22222222-2222-4222-8222-222222222222@ok.example:443?security=tls&type=tcp&flow=xtls-rprx-vision#renamed\n";
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
    char saved[8192];
    size_t saved_len = 0;
    ok("serialize order and expiry",
       store_serialize(&st, saved, sizeof saved, &saved_len) == STORE_OK &&
       strstr(saved, "ORDER ") != NULL && strstr(saved, "SUBMETA ") != NULL);
    store_t restored;
    ok("deserialize order and expiry",
       store_deserialize(&restored, saved, saved_len) == STORE_OK &&
       restored.subs[sub].expire == 1893456000ULL &&
       store_section_at(&restored, 0) == (int)sub &&
       store_section_at(&restored, 1) == STORE_GROUP_MANUAL);

    const size_t before_empty = st.n;
    const char empty[] = "not a server\n";
    added = 99;
    ok("empty refresh rejected", store_refresh_sub(&st, sub, empty, sizeof empty - 1, &added) == STORE_ERR_PARSE);
    ok("empty refresh preserves servers", st.n == before_empty);

    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("all store contract checks passed\n");
    return 0;
}
