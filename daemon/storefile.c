#define _DEFAULT_SOURCE

#include "storefile.h"
#include "settings.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* the store serializes to well under this */
#define STOREFILE_BUF (1024 * 1024)

static char g_buf[STOREFILE_BUF];
static char g_store[STOREFILE_BUF];

int storefile_path_ok(const char *path) {
    if (!path || !path[0]) return 0;
    if (strstr(path, "..") != NULL) return 0;
    if (strncmp(path, "/var/mobile/", 12) != 0 &&
        strncmp(path, "/var/root/", 10) != 0
#if defined(SENKO_HOST_TEST) || !defined(__APPLE__)
        && strncmp(path, "/tmp/", 5) != 0
#endif
        )
        return 0;
    return 1;
}

storefile_status_t storefile_load(store_t *st, daemon_settings_t *set, const char *path) {
    if (!st || !path) return STOREFILE_ERR_ARG;
    if (!storefile_path_ok(path)) return STOREFILE_ERR_PATH;
    if (set) daemon_settings_defaults(set);

    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) {
        if (errno == ENOENT) { /* first run: no config yet, not an error */
            store_init(st);
            return STOREFILE_OK;
        }
        return STOREFILE_ERR_IO;
    }

    struct stat stbuf;
    if (fstat(fd, &stbuf) != 0 || !S_ISREG(stbuf.st_mode)) {
        close(fd);
        return STOREFILE_ERR_IO;
    }

    size_t total = 0;
    for (;;) {
        if (total >= sizeof g_buf) { close(fd); return STOREFILE_ERR_TOOBIG; }
        ssize_t n = read(fd, g_buf + total, sizeof g_buf - total);
        if (n > 0) { total += (size_t)n; continue; }
        if (n == 0) break; /* eof */
        if (errno == EINTR) continue;
        close(fd);
        return STOREFILE_ERR_IO;
    }
    close(fd);

    if (set) daemon_settings_apply_buf(set, g_buf, total);
    if (store_deserialize(st, g_buf, total) != STORE_OK) return STOREFILE_ERR_PARSE;
    return STOREFILE_OK;
}

storefile_status_t storefile_save(const store_t *st, const daemon_settings_t *set,
                                  const char *path) {
    if (!st || !path) return STOREFILE_ERR_ARG;
    if (!storefile_path_ok(path)) return STOREFILE_ERR_PATH;

    size_t store_len = 0;
    if (store_serialize(st, g_store, sizeof g_store, &store_len) != STORE_OK)
        return STOREFILE_ERR_TOOBIG;

    size_t body_off = 0;
    if (store_len >= 3 && memcmp(g_store, "V1\n", 3) == 0) body_off = 3;

    size_t off = 0;
    int n = snprintf(g_buf + off, sizeof g_buf - off, "V1\n");
    if (n < 0 || (size_t)n >= sizeof g_buf - off) return STOREFILE_ERR_TOOBIG;
    off += (size_t)n;

    if (set) {
        size_t set_len = 0;
        if (daemon_settings_serialize(set, g_buf + off, sizeof g_buf - off, &set_len) != 0)
            return STOREFILE_ERR_TOOBIG;
        off += set_len;
    }

    if (body_off < store_len) {
        size_t body_len = store_len - body_off;
        if (off + body_len > sizeof g_buf) return STOREFILE_ERR_TOOBIG;
        memcpy(g_buf + off, g_store + body_off, body_len);
        off += body_len;
    }
    size_t len = off;

    char tmp[1088];
    int pn = snprintf(tmp, sizeof tmp, "%s.tmp.XXXXXX", path);
    if (pn < 0 || (size_t)pn >= sizeof tmp) return STOREFILE_ERR_ARG;

/* use an exclusive temp file so legacy paths cannot redirect the config write */
    int fd = mkstemp(tmp);
    if (fd < 0) return STOREFILE_ERR_IO;
    if (fchmod(fd, 0600) != 0) { close(fd); unlink(tmp); return STOREFILE_ERR_IO; }

    size_t woff = 0;
    while (woff < len) {
        ssize_t wn = write(fd, g_buf + woff, len - woff);
        if (wn > 0) { woff += (size_t)wn; continue; }
        if (errno == EINTR) continue;
        close(fd);
        unlink(tmp);
        return STOREFILE_ERR_IO;
    }

    if (fsync(fd) != 0) { close(fd); unlink(tmp); return STOREFILE_ERR_IO; }
    if (close(fd) != 0) { unlink(tmp); return STOREFILE_ERR_IO; }

    if (rename(tmp, path) != 0) { unlink(tmp); return STOREFILE_ERR_IO; }
    return STOREFILE_OK;
}
