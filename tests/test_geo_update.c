#define _POSIX_C_SOURCE 200809L

#include "geo_update.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

typedef struct { uint8_t *p; size_t n, cap; } buf_t;

static void put(buf_t *b, const void *d, size_t n) {
    if (b->n + n > b->cap) { b->cap = (b->n + n) * 2 + 64; b->p = realloc(b->p, b->cap); }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}
static void varint(buf_t *b, uint64_t v) {
    uint8_t c;
    do { c = v & 0x7f; v >>= 7; if (v) c |= 0x80; put(b, &c, 1); } while (v);
}
static void field(buf_t *b, unsigned f, const buf_t *m) {
    varint(b, f << 3 | 2); varint(b, m->n); put(b, m->p, m->n);
}
static void str(buf_t *b, unsigned f, const char *s) {
    varint(b, f << 3 | 2); varint(b, strlen(s)); put(b, s, strlen(s));
}

static buf_t site_dat(void) {
    buf_t dat = {0}, e = {0}, d = {0};
    str(&e, 1, "RU");
    varint(&d, 1 << 3); varint(&d, 2);
    str(&d, 2, "yandex.ru");
    field(&e, 2, &d);
    field(&dat, 1, &e);
    free(e.p); free(d.p);
    return dat;
}

static buf_t ip_dat(void) {
    buf_t dat = {0}, e = {0}, c = {0};
    const uint8_t net[4] = { 8, 8, 8, 0 };
    str(&e, 1, "RU");
    varint(&c, 1 << 3 | 2); varint(&c, 4); put(&c, net, 4);
    varint(&c, 2 << 3); varint(&c, 24);
    field(&e, 2, &c);
    field(&dat, 1, &e);
    free(e.p); free(c.p);
    return dat;
}

static int g_fetches;
static char g_urls[4][512];
static int g_fail_url;      /* 1: fail the n-th fetch */
static int g_garbage;

static int fake_fetch(void *ctx, const char *url, FILE *out, size_t max_bytes,
                      size_t *written, char *error, size_t error_cap) {
    (void)ctx;
    if (g_fetches < 4) snprintf(g_urls[g_fetches], sizeof g_urls[0], "%s", url);
    ++g_fetches;
    if (g_fail_url == g_fetches) {
        snprintf(error, error_cap, "geo.test answered HTTP 404");
        return -1;
    }
    if (max_bytes != GEO_UPDATE_MAX_BYTES) return -1;
    buf_t dat = strstr(url, "geosite") ? site_dat() : ip_dat();
    if (g_garbage) {
        dat.n = 5;
        memset(dat.p, 0xff, 5);
    }
    fwrite(dat.p, 1, dat.n, out);
    *written = dat.n;
    free(dat.p);
    return 0;
}

static int exists(const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    return access(path, F_OK) == 0;
}

static const char kProfile[] =
    "{\"Name\":\"P\",\"DirectSites\":[\"geosite:ru\",\"geosite:nowhere\"],"
    "\"DirectIp\":[\"geoip:ru\",\"geoip:private\"],"
    "\"Geoipurl\":\"https://geo.test/geoip.dat\",\"Geositeurl\":\"https://geo.test/geosite.dat\"}";

int main(void) {
    char dir[] = "/tmp/senko-geoupd-XXXXXX";
    char geo[300];
    static geo_update_job_t job;
    geo_update_result_t res;
    if (!mkdtemp(dir)) return 2;
    snprintf(geo, sizeof geo, "%s/geo", dir);

    ok("plan wants a download with no files", geo_update_plan(geo, kProfile, strlen(kProfile), 1000, 0, &job) == 1);
    ok("plan tags", job.site_n == 2 && job.ip_n == 1 && strcmp(job.ip_tags[0], "ru") == 0);
    ok("plan urls", strcmp(job.site_url, "https://geo.test/geosite.dat") == 0 &&
       strcmp(job.ip_url, "https://geo.test/geoip.dat") == 0);
    ok("missing count", geo_update_missing(geo, kProfile, strlen(kProfile)) == 3);
    const char *plain = "{\"Name\":\"P\",\"DirectSites\":[\"domain:a.test\"],\"DirectIp\":[\"geoip:private\"]}";
    ok("a profile without geo tags needs nothing", geo_update_plan(geo, plain, strlen(plain), 1000, 1, &job) == 0);

    geo_update_plan(geo, kProfile, strlen(kProfile), 1000, 0, &job);
    g_fetches = 0;
    geo_update_run(&job, fake_fetch, NULL, 1000, &res);
    ok("run ok", res.ok == 1 && g_fetches == 2);
    ok("each dat fetched once", strstr(g_urls[0], "geosite") && strstr(g_urls[1], "geoip"));
    ok("written and missing", res.written == 2 && res.missing == 1);
    ok("missing tag is reported", strstr(res.error, "nowhere") != NULL);
    ok("files written", exists(geo, "site-ru.bin") && exists(geo, "ip-ru.bin"));
    ok("empty file for the tag the dat lacks", exists(geo, "site-nowhere.bin"));
    ok("private needs no file", !exists(geo, "ip-private.bin"));
    ok("download removed", !exists(geo, "download.tmp"));
    ok("senko-core gets a dat of the named codes", exists(geo, "geosite.dat") &&
       exists(geo, "geoip.dat") && !exists(geo, "geosite.dat.new"));
    ok("stamp", geo_update_stamp(geo) == 1000);

    ok("fresh files need no download", geo_update_plan(geo, kProfile, strlen(kProfile), 1000 + 3600, 0, &job) == 0);
    ok("force downloads", geo_update_plan(geo, kProfile, strlen(kProfile), 1000 + 3600, 1, &job) == 1);
    ok("a week later it is due", geo_update_plan(geo, kProfile, strlen(kProfile), 1000 + GEO_UPDATE_STALE_SECONDS, 0, &job) == 1);
    ok("a clock that went back is due", geo_update_plan(geo, kProfile, strlen(kProfile), 10, 0, &job) == 1);

    /* a dropped tag loses its file, and foreign files stay */
    char keep[330];
    snprintf(keep, sizeof keep, "%s/keep.txt", geo);
    FILE *f = fopen(keep, "w");
    if (f) fclose(f);
    const char *one = "{\"DirectSites\":[\"geosite:ru\"],\"Geositeurl\":\"https://geo.test/geosite.dat\"}";
    geo_update_plan(geo, one, strlen(one), 5000, 1, &job);
    g_fetches = 0;
    geo_update_run(&job, fake_fetch, NULL, 5000, &res);
    ok("pruned run ok", res.ok == 1 && g_fetches == 1 && res.missing == 0 && res.error[0] == '\0');
    ok("dropped tags are removed", !exists(geo, "site-nowhere.bin") && !exists(geo, "ip-ru.bin"));
    ok("kept tag and foreign files stay", exists(geo, "site-ru.bin") && exists(geo, "keep.txt"));
    ok("stamp moves", geo_update_stamp(geo) == 5000);

    /* failures keep the old stamp, name the reason and leave no download */
    geo_update_plan(geo, kProfile, strlen(kProfile), 9000, 1, &job);
    g_fetches = 0;
    g_fail_url = 2;
    geo_update_run(&job, fake_fetch, NULL, 9000, &res);
    ok("second download fails", res.ok == 0 && strstr(res.error, "HTTP 404") != NULL);
    ok("failed run keeps the stamp", geo_update_stamp(geo) == 5000);
    ok("failed run leaves no download", !exists(geo, "download.tmp"));
    g_fail_url = 0;
    g_garbage = 1;
    g_fetches = 0;
    geo_update_run(&job, fake_fetch, NULL, 9000, &res);
    ok("garbage is not a dat", res.ok == 0 && strstr(res.error, "not a geo dat") != NULL);
    ok("garbage leaves no download", !exists(geo, "download.tmp") && geo_update_stamp(geo) == 5000);
    g_garbage = 0;

    geo_update_run(NULL, fake_fetch, NULL, 1, &res);
    ok("no job", res.ok == 0 && res.error[0]);
    geo_update_run(&job, NULL, NULL, 1, &res);
    ok("no fetch", res.ok == 0 && res.error[0]);

    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd) != 0) return 2;
    if (failures) {
        fprintf(stderr, "%d geo update checks failed\n", failures);
        return 1;
    }
    puts("all geo update checks passed");
    return 0;
}
