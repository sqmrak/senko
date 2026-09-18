#define _DEFAULT_SOURCE

/* the schedules the daemon runs with no client attached: the startup redial,
   the backoff after a tunnel dies, the failover order and the subscription
   timer. every hook is mocked, so nothing here touches the network */

#include "ctl_server.h"
#include "daemon_ctl.h"
#include "settings.h"
#include "core/control.h"
#include "core/store.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

static int g_fail = 0;

static void ok(const char *what, int cond) {
    if (cond) return;
    g_fail++;
    fprintf(stderr, "FAIL %s\n", what);
}

typedef struct {
    int               calls;
    int               starts;
    ctl_action_kind_t last_kind;
    int               last_index;
    char              last_host[64];
    int               fail_starts; /* fail this many CTL_ACT_START calls */
} apply_rec_t;

static apply_rec_t g_apply;

static int mock_apply(void *ctx, const ctl_action_t *a) {
    (void)ctx;
    g_apply.calls++;
    g_apply.last_kind = a->kind;
    if (a->kind != CTL_ACT_START) return 0;
    g_apply.starts++;
    g_apply.last_index = a->server_index;
    snprintf(g_apply.last_host, sizeof g_apply.last_host, "%s", a->server.host);
    if (g_apply.fail_starts > 0) {
        g_apply.fail_starts--;
        return DCTL_ERR_DNS;
    }
    return 0;
}

typedef struct {
    int calls;
    const char *blob;
    int fail_next;
} fetch_rec_t;

static fetch_rec_t g_fetch;

static int mock_fetch(void *ctx, const char *url, const char *request_header,
                      unsigned char *buf, size_t cap, size_t *len,
                      ctl_fetch_meta_t *meta) {
    (void)ctx; (void)url; (void)request_header;
    if (meta) memset(meta, 0, sizeof *meta);
    g_fetch.calls++;
    if (g_fetch.fail_next) { g_fetch.fail_next--; return -1; }
    size_t bl = strlen(g_fetch.blob);
    if (bl > cap) return -1;
    memcpy(buf, g_fetch.blob, bl);
    *len = bl;
    return 0;
}

static int g_persist_calls;

static void mock_persist(void *ctx, const store_t *store) {
    (void)ctx; (void)store;
    g_persist_calls++;
}

/* the same monotonic source the schedules use, so the deadlines below can be
   compared against it directly */
static long now_ms(void) {
#ifdef __APPLE__
    mach_timebase_info_data_t scale;
    mach_timebase_info(&scale);
    return (long)((double)mach_absolute_time() * scale.numer / scale.denom / 1000000.0);
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
#endif
}

/* the redial waits a real second, and a test that slept through every backoff
   step would take a minute, so the deadline is moved into the past instead */
static void fire_retry(ctl_server_t *s) {
    ok("a retry was pending", s->retry_at_ms != 0);
    s->retry_at_ms = 1;
    ctl_server_tick(s);
}

static void go_connected(ctl_server_t *s) {
    char ev[64]; size_t en = 0;
    ctl_engine_notify(&s->engine, CTL_STATE_CONNECTED, ev, sizeof ev, &en);
}

int main(void) {
    const char *path = "/tmp/senko_autoconnect.sock";
    ctl_server_t s;
    daemon_settings_t set;

    unlink(path);
    daemon_settings_defaults(&set);
    ok("server init", ctl_server_init(&s, path, mock_apply, NULL) == CTLS_OK);
    ctl_server_set_settings(&s, &set);
    ctl_server_set_persist(&s, mock_persist);

    size_t idx = 0;
    ok("server a", store_add_manual(&s.engine.store,
       "vless://aaaa1111-6324-4d53-ad4f-8cda48b30811@1.1.1.1:443?security=none&type=tcp#A",
       &idx) == STORE_OK);
    ok("server b", store_add_manual(&s.engine.store,
       "vless://bbbb2222-6324-4d53-ad4f-8cda48b30811@2.2.2.2:443?security=none&type=tcp#B",
       &idx) == STORE_OK);
    ok("server c", store_add_manual(&s.engine.store,
       "vless://cccc3333-6324-4d53-ad4f-8cda48b30811@3.3.3.3:443?security=none&type=tcp#C",
       &idx) == STORE_OK);

    /* startup redial */
    memset(&g_apply, 0, sizeof g_apply);
    s.engine.store.selected = -1;
    ok("restore without a selection does nothing",
       ctl_server_restore_tunnel(&s) != 0 && g_apply.starts == 0);

    store_select(&s.engine.store, 1);
    ok("restore starts the stored server",
       ctl_server_restore_tunnel(&s) == 0 && g_apply.starts == 1 &&
       strcmp(g_apply.last_host, "2.2.2.2") == 0);
    ok("restore reports connected", s.engine.state == CTL_STATE_CONNECTED);
    ok("restore of a live tunnel is a no-op",
       ctl_server_restore_tunnel(&s) == 0 && g_apply.starts == 1);

    /* a lost tunnel with the redial turned off stays lost */
    set.auto_reconnect = 0;
    ctl_server_tunnel_lost(&s);
    ok("no redial without the setting",
       s.engine.state == CTL_STATE_ERROR && s.retry_at_ms == 0 &&
       s.retry_attempts == 0);

    /* a lost tunnel with the redial on comes back */
    set.auto_reconnect = 1;
    go_connected(&s);
    memset(&g_apply, 0, sizeof g_apply);
    long before = now_ms();
    ctl_server_tunnel_lost(&s);
    ok("first redial is scheduled a second out",
       s.retry_attempts == 1 && s.retry_at_ms >= before + 1000 &&
       s.retry_at_ms <= before + 1500);
    ok("a lost tunnel reads as connecting, not failed",
       s.engine.state == CTL_STATE_CONNECTING);
    ok("nothing is dialled before the deadline",
       (ctl_server_tick(&s), g_apply.starts == 0));

    fire_retry(&s);
    ok("the redial dialled the stored server",
       g_apply.starts == 1 && strcmp(g_apply.last_host, "2.2.2.2") == 0);
    ok("a successful redial clears the schedule",
       s.engine.state == CTL_STATE_CONNECTED && s.retry_attempts == 0 &&
       s.retry_at_ms == 0);

    /* the backoff doubles while the server stays unreachable */
    go_connected(&s);
    memset(&g_apply, 0, sizeof g_apply);
    g_apply.fail_starts = 3;
    ctl_server_tunnel_lost(&s);
    before = now_ms();
    fire_retry(&s);
    ok("a failed redial schedules another", s.retry_attempts == 2 && s.retry_at_ms != 0);
    ok("the second wait is two seconds",
       s.retry_at_ms - before >= 2000 && s.retry_at_ms - before <= 2500);
    before = now_ms();
    fire_retry(&s);
    ok("the third wait is four seconds",
       s.retry_attempts == 3 && s.retry_at_ms - before >= 4000 &&
       s.retry_at_ms - before <= 4500);

    /* the wait stops growing, so a phone that comes back is picked up fast */
    set.reconnect_max_attempts = 0; /* keep trying, which is what the cap is for */
    s.retry_attempts = 12;
    g_apply.fail_starts = 1;
    before = now_ms();
    s.retry_at_ms = 1;
    ctl_server_tick(&s);
    ok("the wait is capped at thirty seconds",
       s.retry_at_ms <= before + 30500 && s.retry_at_ms >= before + 29500);

    /* the attempt limit ends it instead of retrying forever */
    set.reconnect_max_attempts = 2;
    s.retry_attempts = 0;
    s.retry_at_ms = 0;
    go_connected(&s);
    g_apply.fail_starts = 5;
    ctl_server_tunnel_lost(&s);
    fire_retry(&s);
    ok("the second attempt is still scheduled", s.retry_attempts == 2);
    fire_retry(&s);
    ok("the limit ends the schedule",
       s.retry_at_ms == 0 && s.retry_attempts == 0 &&
       s.engine.state == CTL_STATE_ERROR);
    set.reconnect_max_attempts = SENKO_DEFAULT_RECONNECT_ATTEMPTS;

    /* failover walks the section in ping order */
    s.ping_ms[0] = 200;
    s.ping_ms[1] = 50;
    s.ping_ms[2] = -1; /* never measured, so it goes last */
    store_select(&s.engine.store, 0);
    s.engine.state = CTL_STATE_IDLE;

    set.failover = 0;
    memset(&g_apply, 0, sizeof g_apply);
    g_apply.fail_starts = 5;
    ok("without failover one server is tried",
       ctl_server_restore_tunnel(&s) != 0 && g_apply.starts == 1);

    set.failover = 1;
    s.engine.state = CTL_STATE_IDLE;
    memset(&g_apply, 0, sizeof g_apply);
    g_apply.fail_starts = 2; /* the requested server and the fastest one fail */
    ok("failover reaches the third server",
       ctl_server_restore_tunnel(&s) == 0 && g_apply.starts == 3 &&
       strcmp(g_apply.last_host, "3.3.3.3") == 0);
    ok("failover follows the measured latency", g_apply.last_index == 2);
    ok("the server that came up becomes the selection",
       s.engine.store.selected == 2);

    /* a subscription node is never a silent substitute for a manual one */
    s.engine.store.group[2] = 0;
    s.engine.store.subs[0].used = 1;
    snprintf(s.engine.store.subs[0].name, sizeof s.engine.store.subs[0].name, "panel");
    snprintf(s.engine.store.subs[0].url, sizeof s.engine.store.subs[0].url,
             "https://example.com/sub");
    store_select(&s.engine.store, 0);
    s.engine.state = CTL_STATE_IDLE;
    memset(&g_apply, 0, sizeof g_apply);
    g_apply.fail_starts = 5;
    ok("failover stays inside the section",
       ctl_server_restore_tunnel(&s) != 0 && g_apply.starts == 2);
    s.engine.store.group[2] = STORE_GROUP_MANUAL;

    /* the subscription timer */
    set.failover = 0;
    set.sub_refresh_hours = 1;
    s.engine.state = CTL_STATE_IDLE;
    s.retry_at_ms = 0;
    g_fetch.blob =
        "vless://dddd4444-6324-4d53-ad4f-8cda48b30811@4.4.4.4:443?security=none&type=tcp#D";
    ctl_server_set_fetch(&s, mock_fetch);
    memset(&g_fetch.calls, 0, sizeof g_fetch.calls);
    g_persist_calls = 0;

    s.sub_check_ms = 0;
    ctl_server_tick(&s);
    ok("a subscription that was never pulled is refreshed", g_fetch.calls == 1);
    ok("the refresh time is recorded", s.engine.store.subs[0].last_refresh != 0);
    ok("the refresh is saved", g_persist_calls > 0);

    s.sub_check_ms = 0;
    ctl_server_tick(&s);
    ok("a fresh subscription is left alone", g_fetch.calls == 1);

    /* a panel that is down must not be pulled once a minute all day */
    s.engine.store.subs[0].last_refresh = 0;
    s.sub_check_ms = 0;
    g_fetch.fail_next = 1;
    ctl_server_tick(&s);
    ok("a failed refresh was attempted", g_fetch.calls == 2);
    ok("a failed refresh backs off", s.sub_retry_at_ms[0] > now_ms());
    s.sub_check_ms = 0;
    ctl_server_tick(&s);
    ok("the backoff holds the next attempt", g_fetch.calls == 2);

    set.sub_refresh_hours = 0;
    s.sub_check_ms = 0;
    s.sub_retry_at_ms[0] = 0;
    s.engine.store.subs[0].last_refresh = 0;
    ctl_server_tick(&s);
    ok("the timer off means no refresh", g_fetch.calls == 2);

    ctl_server_close(&s);
    unlink(path);

    if (g_fail) { fprintf(stderr, "%d check(s) failed\n", g_fail); return 1; }
    printf("all autoconnect checks passed\n");
    return 0;
}
