#include "senko_upload.h"
#include "session.h"

#ifdef SENKO_RELEASE

void senko_upload_in_feed(session_t *s, const uint8_t *buf, size_t len) {
    (void)s; (void)buf; (void)len;
}

void senko_upload_wire_feed(session_t *s, const uint8_t *buf, size_t len) {
    (void)s; (void)buf; (void)len;
}

void senko_upload_flush(session_t *s) {
    (void)s;
}

#else

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#define UPLOAD_MAGIC "SNKU"
#define UPLOAD_DIR   "/var/log/senko-upload"

#define UPLOAD_BUF_MAX (64 * 1024)

static void upload_reset(session_t *s) {
    s->upload_in_len = 0;
    s->upload_in_chunks = 0;
    s->upload_wire_len = 0;
    s->upload_wire_chunks = 0;
    s->upload_started = 0;
}

static int upload_append(uint8_t *buf, size_t *blen, uint32_t *chunks,
                         const uint8_t *src, size_t len) {
    if (!src || len == 0) return 0;
    if (len > 0xffffffffu) return -1;
    if (*blen + 4 + len > UPLOAD_BUF_MAX) return -1;
    uint32_t n = (uint32_t)len;
    memcpy(buf + *blen, &n, 4);
    *blen += 4;
    memcpy(buf + *blen, src, len);
    *blen += len;
    (*chunks)++;
    return 0;
}

void senko_upload_in_feed(session_t *s, const uint8_t *buf, size_t len) {
    if (!s || !s->vision_on || !buf || len == 0) return;
    if (!s->upload_uuid_set) {
        memcpy(s->upload_uuid, s->u.vc.uuid, 16);
        s->upload_uuid_set = 1;
    }
    s->upload_started = 1;
    (void)upload_append(s->upload_in_buf, &s->upload_in_len, &s->upload_in_chunks,
                        buf, len);
}

void senko_upload_wire_feed(session_t *s, const uint8_t *buf, size_t len) {
    if (!s || !s->vision_on || !s->upload_started || !buf || len == 0) return;
    (void)upload_append(s->upload_wire_buf, &s->upload_wire_len, &s->upload_wire_chunks,
                        buf, len);
}

static int sanitize_host_char(unsigned char c) {
    if (c >= 'a' && c <= 'z') return c;
    if (c >= 'A' && c <= 'Z') return c;
    if (c >= '0' && c <= '9') return c;
    if (c == '.' || c == '-' || c == '_') return c;
    return '_';
}

static void make_upload_path(const session_t *s, char *out, size_t cap) {
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

    snprintf(out, cap, UPLOAD_DIR "/%s_%lu.bin", safe, ms);
}

void senko_upload_flush(session_t *s) {
    if (!s || !s->vision_on || !s->upload_started) return;
    if (s->upload_in_chunks == 0 && s->upload_wire_chunks == 0) return;

    char path[256];
    make_upload_path(s, path, sizeof path);

    FILE *f = fopen(path, "wb");
    if (!f) {
        (void)mkdir(UPLOAD_DIR, 0755);
        f = fopen(path, "wb");
        if (!f) return;
    }

    uint16_t host_len = 0;
    size_t host_sz = strlen(s->trace_host);
    if (host_sz > 0xffff) host_sz = 0xffff;
    host_len = (uint16_t)host_sz;

    fwrite(UPLOAD_MAGIC, 1, 4, f);
    fwrite(&host_len, 1, 2, f);
    if (host_len > 0) fwrite(s->trace_host, 1, host_len, f);
    fwrite(s->upload_uuid, 1, 16, f);
    fwrite(&s->upload_in_chunks, 1, 4, f);
    fwrite(s->upload_in_buf, 1, s->upload_in_len, f);
    fwrite(&s->upload_wire_chunks, 1, 4, f);
    fwrite(s->upload_wire_buf, 1, s->upload_wire_len, f);
    fclose(f);

    fprintf(stderr,
            "senko-upload: wrote %s in_chunks=%u wire_chunks=%u "
            "in_bytes=%zu wire_bytes=%zu host=%s\n",
            path, s->upload_in_chunks, s->upload_wire_chunks,
            s->upload_in_len, s->upload_wire_len,
            s->trace_host[0] ? s->trace_host : "-");
    fflush(stderr);

    upload_reset(s);
}

#endif