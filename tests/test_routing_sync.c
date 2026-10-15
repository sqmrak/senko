#include "routing_sync.h"

#include "b64.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

static char *link_of(const char *prefix, const char *json) {
    size_t n = strlen(json), cap = b64_encoded_maxlen(n) + 4, out = 0;
    char *b = malloc(cap + strlen(prefix) + 1);
    strcpy(b, prefix);
    if (b64_encode((const unsigned char *)json, n, b + strlen(prefix), cap, &out) != 0) exit(2);
    b[strlen(prefix) + out] = '\0';
    return b;
}

static const char kA[] = "{\"Name\":\"A\",\"DirectSites\":[\"domain:a.test\"]}";
static const char kB[] = "{\"Name\":\"B\",\"GlobalProxy\":\"false\",\"ProxySites\":[\"domain:b.test\"]}";

int main(void) {
    static store_t st;
    routing_sync_t r;
    size_t sa = 0, sb = 0;
    store_init(&st);
    store_add_sub(&st, "A", "https://a.example/x", &sa);
    store_add_sub(&st, "B", "https://b.example/x", &sb);
    char *add_a = link_of("happ://routing/add/", kA);
    char *onadd_a = link_of("happ://routing/onadd/", kA);
    char *add_b = link_of("happ://routing/add/", kB);
    char *onadd_b = link_of("happ://routing/onadd/", kB);

    routing_sync_apply(&st, sa, NULL, 0, "vless://x\n", 10, &r);
    ok("nothing sent", r.kind == ROUTING_SYNC_NONE && st.routing_active == -1 && !r.message[0]);

    routing_sync_apply(&st, sa, add_a, 0, "", 0, &r);
    ok("first add becomes active", r.kind == ROUTING_SYNC_SET && r.activated && st.routing_active == (int)sa &&
       strcmp(store_sub_routing(&st, sa), kA) == 0);
    routing_sync_apply(&st, sa, add_a, 0, "", 0, &r);
    ok("same profile is unchanged", r.kind == ROUTING_SYNC_UNCHANGED && r.activated);

    routing_sync_apply(&st, sb, add_b, 0, "", 0, &r);
    ok("a later add does not take over", r.kind == ROUTING_SYNC_SET && !r.activated &&
       st.routing_active == (int)sa && strcmp(store_sub_routing(&st, sb), kB) == 0);
    routing_sync_apply(&st, sb, onadd_b, 0, "", 0, &r);
    ok("onadd takes over", r.kind == ROUTING_SYNC_UNCHANGED && r.activated && st.routing_active == (int)sb);
    routing_sync_apply(&st, sa, onadd_a, 0, "", 0, &r);
    ok("onadd of the other one", r.activated && st.routing_active == (int)sa);

    /* the body line works without a header */
    char body[1024];
    snprintf(body, sizeof body, "vless://a@b:1\n%s\n", onadd_b);
    routing_sync_apply(&st, sb, NULL, 0, body, strlen(body), &r);
    ok("body line", r.activated && st.routing_active == (int)sb);

    /* a panel withdraws its own profile only */
    routing_sync_apply(&st, sa, "happ://routing/off", 0, "", 0, &r);
    ok("off removes the panel's profile", r.kind == ROUTING_SYNC_OFF && store_sub_routing(&st, sa)[0] == '\0' &&
       st.routing_active == (int)sb && r.message[0]);
    routing_sync_apply(&st, sa, "happ://routing/off", 0, "", 0, &r);
    ok("off with nothing stored says nothing", r.kind == ROUTING_SYNC_NONE && !r.message[0]);
    routing_sync_apply(&st, sb, "happ://routing/off", 0, "", 0, &r);
    ok("off of the active one switches routing off", r.kind == ROUTING_SYNC_OFF && st.routing_active == -1);

    /* what cannot be used is refused and changes nothing */
    routing_sync_apply(&st, sa, onadd_a, 0, "", 0, &r);
    ok("setup", st.routing_active == (int)sa);
    routing_sync_apply(&st, sa, "happ://routing/onadd/@@@", 0, "", 0, &r);
    ok("bad link refused", r.kind == ROUTING_SYNC_REFUSED && r.message[0] && !r.activated &&
       strcmp(store_sub_routing(&st, sa), kA) == 0 && st.routing_active == (int)sa);
    char *notprofile = link_of("happ://routing/onadd/", "{\"unrelated\":1}");
    routing_sync_apply(&st, sa, notprofile, 0, "", 0, &r);
    ok("json that is no profile refused", r.kind == ROUTING_SYNC_REFUSED && strcmp(store_sub_routing(&st, sa), kA) == 0);
    routing_sync_apply(&st, sa, "", 1, "", 0, &r);
    ok("too long header refused", r.kind == ROUTING_SYNC_REFUSED && strstr(r.message, "too long") != NULL);
    routing_sync_apply(&st, 31, onadd_a, 0, "", 0, &r);
    ok("unknown subscription refused", r.kind == ROUTING_SYNC_REFUSED);
    routing_sync_apply(NULL, 0, onadd_a, 0, "", 0, &r);
    ok("no store refused", r.kind == ROUTING_SYNC_REFUSED);

    free(notprofile);
    free(add_a); free(onadd_a); free(add_b); free(onadd_b);
    if (failures) {
        fprintf(stderr, "%d routing sync checks failed\n", failures);
        return 1;
    }
    puts("all routing sync checks passed");
    return 0;
}
