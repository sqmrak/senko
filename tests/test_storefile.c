#define _DEFAULT_SOURCE

#include "storefile.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_fail = 0;

static void ok(const char *what, int cond) {
    if (cond) return;
    g_fail++;
    fprintf(stderr, "FAIL %s\n", what);
}

static int write_text(const char *path, const char *text) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    size_t len = strlen(text);
    int rc = write(fd, text, len) == (ssize_t)len ? 0 : -1;
    close(fd);
    return rc;
}

static int read_text(const char *path, char *buf, size_t cap) {
    int fd = open(path, O_RDONLY);
    if (fd < 0 || cap == 0) return -1;
    ssize_t n = read(fd, buf, cap - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    return 0;
}

int main(void) {
    const char *cfg = "/tmp/senko-storefile.cfg";
    const char *tmp = "/tmp/senko-storefile.cfg.tmp";
    const char *victim = "/tmp/senko-storefile.victim";
    unlink(cfg);
    unlink(tmp);
    unlink(victim);

    store_t st;
    store_init(&st);
    size_t index = 0;
    ok("server added", store_add_manual(&st,
       "vless://11111111-1111-4111-8111-111111111111@1.2.3.4:443?security=none&type=tcp#test",
       &index) == STORE_OK);

    daemon_settings_t settings;
    daemon_settings_defaults(&settings);
    ok("victim create", write_text(victim, "sentinel\n") == 0);
    ok("old predictable temp symlink", symlink(victim, tmp) == 0);
    ok("save ignores planted temp symlink",
       storefile_save(&st, &settings, cfg) == STOREFILE_OK);

    char text[64];
    ok("victim unchanged", read_text(victim, text, sizeof text) == 0 &&
       strcmp(text, "sentinel\n") == 0);

    store_t loaded;
    daemon_settings_t loaded_settings;
    ok("saved config loads", storefile_load(&loaded, &loaded_settings, cfg) == STOREFILE_OK &&
       loaded.n == 1 && loaded_settings.socks_port == settings.socks_port);

/* the automation settings live in the same file as the catalog, so a value the
   control protocol wrote has to come back after a daemon restart */
    settings.auto_connect = 1;
    settings.auto_reconnect = 0;
    settings.reconnect_max_attempts = 9;
    settings.sub_refresh_hours = 6;
    settings.failover = 1;
    settings.block_response = DNS_BLOCK_NXDOMAIN;
    ok("persistent rule added", store_add_rule(&st,
       "block domain-keyword tracker", 28, NULL) == STORE_OK);
    st.subs[0].used = 1;
    snprintf(st.subs[0].name, sizeof st.subs[0].name, "panel");
    snprintf(st.subs[0].url, sizeof st.subs[0].url, "https://example.com/sub");
    st.subs[0].last_refresh = 1789000000ull;
    unlink(cfg);
    ok("save with automation settings",
       storefile_save(&st, &settings, cfg) == STOREFILE_OK);
    ok("automation settings survive save and load",
       storefile_load(&loaded, &loaded_settings, cfg) == STOREFILE_OK &&
       loaded_settings.auto_connect == 1 &&
       loaded_settings.auto_reconnect == 0 &&
       loaded_settings.reconnect_max_attempts == 9 &&
       loaded_settings.sub_refresh_hours == 6 &&
       loaded_settings.failover == 1 &&
       loaded_settings.block_response == DNS_BLOCK_NXDOMAIN &&
       loaded.rules.count == 1 &&
       ruleset_match_domain(&loaded.rules, "cdn.tracker.test", NULL) ==
           RULE_ACTION_BLOCK);
    ok("subscription refresh time survives save and load",
       loaded.subs[0].used && loaded.subs[0].last_refresh == 1789000000ull);

/* a config written by a newer build carries keys this one has no field for, and
   dropping the file over one of them would lose every server in it */
    ok("unknown key does not break the file",
       write_text(cfg,
                  "SET socks_port 12345\n"
                  "SET quantum_tunnel 1\n"
                  "SET auto_connect 1\n"
                  "SRV -1 vless://11111111-1111-4111-8111-111111111111@1.2.3.4:443?security=none&type=tcp#test\n"
                  "SEL 0\n") == 0);
    ok("known keys around an unknown one still apply",
       storefile_load(&loaded, &loaded_settings, cfg) == STOREFILE_OK &&
       loaded.n == 1 && loaded_settings.socks_port == 12345 &&
       loaded_settings.auto_connect == 1);

/* the developer overrides live in the config beside the rest, so a daemon that
   restarts under a pinned rung comes back on the same rung */
    ok("developer overrides round trip",
       write_text(cfg,
                  "SET force_backend c\n"
                  "SET force_pf_mode 5\n"
                  "SET sub_ignore_gating 1\n"
                  "SET trace 1\n"
                  "SET socks_public 1\n"
                  "SET block_response nxdomain\n") == 0);
    ok("every override comes back as it was written",
       storefile_load(&loaded, &loaded_settings, cfg) == STOREFILE_OK &&
       loaded_settings.force_backend == SENKO_BACKEND_C &&
       loaded_settings.force_pf_mode == 5 &&
       loaded_settings.sub_ignore_gating == 1 &&
       loaded_settings.trace == 1 &&
       loaded_settings.socks_public == 1 &&
       loaded_settings.block_response == DNS_BLOCK_NXDOMAIN);

    {
        daemon_settings_t probe;
        daemon_settings_defaults(&probe);
        ok("the defaults pin nothing",
           probe.force_backend == SENKO_BACKEND_AUTO &&
           probe.force_pf_mode == SENKO_PF_MODE_AUTO &&
           probe.trace == 0);
        ok("auto restores the ladder",
           daemon_settings_set(&probe, "force_pf_mode", 13, "auto", 4) == SETTINGS_OK &&
           probe.force_pf_mode == SENKO_PF_MODE_AUTO);
        ok("a pf variant this build does not have is refused",
           daemon_settings_set(&probe, "force_pf_mode", 13, "8", 1) == SETTINGS_ERR_VALUE);
        ok("a backend name this build does not have is refused",
           daemon_settings_set(&probe, "force_backend", 13, "rust", 4) == SETTINGS_ERR_VALUE);
        char dump[1024];
        size_t dump_len = 0;
        ok("the dump emits every override in the words SET takes back",
           daemon_settings_serialize(&probe, dump, sizeof dump, &dump_len) == 0 &&
           strstr(dump, "SET force_backend auto\n") != NULL &&
           strstr(dump, "SET force_pf_mode auto\n") != NULL &&
           strstr(dump, "SET sub_ignore_gating 0\n") != NULL &&
           strstr(dump, "SET trace 0\n") != NULL);
    }

    unlink(cfg);
    ok("config symlink", symlink(victim, cfg) == 0);
    ok("load rejects symlink", storefile_load(&loaded, &loaded_settings, cfg) == STOREFILE_ERR_IO);

    unlink(cfg);
    unlink(tmp);
    unlink(victim);
    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("all storefile checks passed\n");
    return 0;
}
