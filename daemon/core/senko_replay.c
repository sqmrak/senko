#include "senko_replay.h"
#include "session.h"

#ifdef SENKO_RELEASE

void senko_replay_feed(session_t *s, const uint8_t *buf, size_t len) {
    (void)s; (void)buf; (void)len;
}

void senko_replay_flush(session_t *s) {
    (void)s;
}

#else

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#define REPLAY_MAGIC "SNK1"
#define REPLAY_DIR   "/var/log/senko-replay"

#define REPLAY_BUF_MAX (64 * 1024)

static void replay_reset(session_t *s) {
    s->replay_len = 0;
    s->replay_chunks = 0;
}

static int replay_append(session_t *s, const uint8_t *buf, size_t len) {
    if (!buf || len == 0) return 0;
    if (len > 0xffffffffu) return -1;
    if (s->replay_len + 4 + len > REPLAY_BUF_MAX) return -1;
    uint32_t n = (uint32_t)len;
    memcpy(s->replay_buf + s->replay_len, &n, 4);
    s->replay_len += 4;
    memcpy(s->replay_buf + s->replay_len, buf, len);
    s->replay_len += len;
    s->replay_chunks++;
    return 0;
}

void senko_replay_feed(session_t *s, const uint8_t *buf, size_t len) {
    if (!s || !s->vision_on || s->vision_downstream_direct) return;
    if (!s->replay_uuid_set) {
        memcpy(s->replay_uuid, s->u.vc.uuid, 16);
        s->replay_uuid_set = 1;
    }
    (void)replay_append(s, buf, len);
}

static int sanitize_host_char(unsigned char c) {
    if (c >= 'a' && c <= 'z') return c;
    if (c >= 'A' && c <= 'Z') return c;
    if (c >= '0' && c <= '9') return c;
    if (c == '.' || c == '-' || c == '_') return c;
    return '_';
}

static void make_replay_path(const session_t *s, char *out, size_t cap) {
    unsigned long ms = 0;
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0)
        ms = (unsigned long)tv.tv_sec * 1000ul + (unsigned long)tv.tv_usec / 1000ul;

    char safe[128];
    size_t si = 0;
    for (const char *p = s->trace_host; *p && si + 1 < sizeof safe; ++p)
        safe[si++] = (char)sanitize_host_char((unsigned char)*p);
    safe[si] = '\0';
    if (!safe[0]) snprintf(safe, sizeof safe, "unknown");

    snprintf(out, cap, REPLAY_DIR "/%s_%lu.bin", safe, ms);
}

void senko_replay_flush(session_t *s) {
    if (!s || !s->vision_on || s->replay_len == 0 || s->replay_chunks == 0) return;

    char path[256];
    make_replay_path(s, path, sizeof path);

    FILE *f = fopen(path, "wb");
    if (!f) {
        (void)mkdir(REPLAY_DIR, 0755);
        f = fopen(path, "wb");
        if (!f) return;
    }

    uint16_t host_len = 0;
    size_t host_sz = strlen(s->trace_host);
    if (host_sz > 0xffff) host_sz = 0xffff;
    host_len = (uint16_t)host_sz;

    fwrite(REPLAY_MAGIC, 1, 4, f);
    fwrite(&host_len, 1, 2, f);
    if (host_len > 0) fwrite(s->trace_host, 1, host_len, f);
    fwrite(s->replay_uuid, 1, 16, f);
    {
        uint32_t nch = (uint32_t)s->replay_chunks;
        fwrite(&nch, 1, 4, f);
    }
    fwrite(s->replay_buf, 1, s->replay_len, f);
    fclose(f);

    fprintf(stderr, "senko-replay: wrote %s chunks=%u bytes=%zu host=%s\n",
            path, (unsigned)s->replay_chunks, s->replay_len,
            s->trace_host[0] ? s->trace_host : "-");
    fflush(stderr);

    replay_reset(s);
}

#endif