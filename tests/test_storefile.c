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
