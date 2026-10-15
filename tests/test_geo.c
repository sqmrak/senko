#define _POSIX_C_SOURCE 200809L

#include "geo.h"

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

typedef struct {
    uint8_t *p;
    size_t n, cap;
} buf_t;

static void put(buf_t *b, const void *data, size_t n) {
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 64;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, data, n);
    b->n += n;
}

static void put_varint(buf_t *b, uint64_t v) {
    uint8_t c;
    do {
        c = v & 0x7f;
        v >>= 7;
        if (v) c |= 0x80;
        put(b, &c, 1);
    } while (v);
}

static void put_field(buf_t *b, unsigned field, const buf_t *msg) {
    put_varint(b, field << 3 | 2);
    put_varint(b, msg->n);
    put(b, msg->p, msg->n);
}

static void put_str(buf_t *b, unsigned field, const char *s) {
    put_varint(b, field << 3 | 2);
    put_varint(b, strlen(s));
    put(b, s, strlen(s));
}

static void put_num(buf_t *b, unsigned field, uint64_t v) {
    put_varint(b, field << 3);
    put_varint(b, v);
}

static void domain(buf_t *entry, unsigned type, const char *value, const char *attr) {
    buf_t d = {0};
    put_num(&d, 1, type);
    put_str(&d, 2, value);
    if (attr) {
        buf_t a = {0};
        put_str(&a, 1, attr);
        put_num(&a, 2, 1);
        put_field(&d, 3, &a);
        free(a.p);
    }
    put_field(entry, 2, &d);
    free(d.p);
}

static void cidr(buf_t *entry, const uint8_t *ip, size_t len, unsigned prefix) {
    buf_t c = {0};
    put_varint(&c, 1 << 3 | 2);
    put_varint(&c, len);
    put(&c, ip, len);
    put_num(&c, 2, prefix);
    put_field(entry, 2, &c);
    free(c.p);
}

static void write_file(const char *path, const buf_t *b) {
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(b->p, 1, b->n, f) != b->n) {
        perror(path);
        exit(2);
    }
    fclose(f);
}

static int ip4(const geo_ip_t *s, unsigned a, unsigned b, unsigned c, unsigned d) {
    uint8_t x[4] = { (uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d };
    return geo_ip_match(s, x, 4);
}

static void test_site_set(const char *dir) {
    geo_site_t *s = geo_site_new();
    ok("site add suffix", geo_site_add(s, GEO_SITE_SUFFIX, "Example.COM.", 12) == GEO_OK);
    ok("site add full", geo_site_add(s, GEO_SITE_FULL, "only.test", 9) == GEO_OK);
    ok("site add keyword", geo_site_add(s, GEO_SITE_KEYWORD, "track", 5) == GEO_OK);
    ok("site add dup", geo_site_add(s, GEO_SITE_SUFFIX, "example.com", 11) == GEO_OK);
    ok("site add second", geo_site_add(s, GEO_SITE_SUFFIX, "second.test", 11) == GEO_OK);
    ok("site bad empty", geo_site_add(s, GEO_SITE_SUFFIX, "", 0) == GEO_ERR_ARG);
    ok("site bad space", geo_site_add(s, GEO_SITE_SUFFIX, "a b", 3) == GEO_ERR_ARG);
    ok("site unsealed no match", !geo_site_match(s, "example.com"));
    geo_site_seal(s);
    ok("site dedup", geo_site_count(s) == 4);
    ok("suffix self", geo_site_match(s, "example.com"));
    ok("suffix child", geo_site_match(s, "a.b.example.com"));
    ok("suffix boundary", !geo_site_match(s, "notexample.com"));
    ok("suffix parent", !geo_site_match(s, "com"));
    ok("full self", geo_site_match(s, "only.test"));
    ok("full child", !geo_site_match(s, "a.only.test"));
    ok("keyword", geo_site_match(s, "cdn-tracker.net"));
    ok("miss", !geo_site_match(s, "other.net"));
    ok("empty domain", !geo_site_match(s, ""));

    char path[512];
    snprintf(path, sizeof path, "%s/s.bin", dir);
    ok("site save", geo_site_save(s, path) == GEO_OK);
    geo_site_t *back = NULL;
    ok("site load", geo_site_load(path, &back) == GEO_OK && back);
    ok("site roundtrip", back && geo_site_match(back, "x.example.com") &&
       geo_site_match(back, "only.test") && !geo_site_match(back, "a.only.test") &&
       geo_site_match(back, "my-track.org") && geo_site_count(back) == 4);
    geo_site_free(back);

    /* a file whose hashes are out of order must be refused, not searched */
    FILE *f = fopen(path, "r+b");
    uint8_t swap[16];
    fseek(f, 20, SEEK_SET);
    fread(swap, 1, 16, f);
    uint8_t flip[16];
    memcpy(flip, swap + 8, 8);
    memcpy(flip + 8, swap, 8);
    fseek(f, 20, SEEK_SET);
    fwrite(flip, 1, 16, f);
    fclose(f);
    ok("site unsorted refused", geo_site_load(path, &back) == GEO_ERR_FORMAT && !back);
    truncate(path, 24);
    ok("site truncated refused", geo_site_load(path, &back) == GEO_ERR_FORMAT);
    f = fopen(path, "wb");
    fwrite("XXXX", 1, 4, f);
    fclose(f);
    ok("site bad magic", geo_site_load(path, &back) == GEO_ERR_FORMAT);
    ok("site missing file", geo_site_load("/nonexistent/geo.bin", &back) == GEO_ERR_IO);
    geo_site_free(s);
}

static void test_ip_set(const char *dir) {
    geo_ip_t *s = geo_ip_new();
    const uint8_t a[4] = { 10, 0, 0, 0 }, b[4] = { 10, 1, 0, 0 }, c[4] = { 11, 0, 0, 0 };
    const uint8_t all[4] = { 0 };
    ok("ip add", geo_ip_add(s, a, 4, 16) == GEO_OK);
    ok("ip add nested", geo_ip_add(s, b, 4, 24) == GEO_OK);
    ok("ip add adjacent", geo_ip_add(s, c, 4, 8) == GEO_OK);
    ok("ip bad prefix", geo_ip_add(s, a, 4, 33) == GEO_ERR_ARG);
    ok("ip bad len", geo_ip_add(s, a, 5, 8) == GEO_ERR_ARG);
    geo_ip_seal(s);
    /* 10.0/16 and 10.1/24 touch and merge, 11/8 is separated by a gap */
    ok("ip merged", geo_ip_count(s) == 2);
    ok("ip in", ip4(s, 10, 0, 5, 5) && ip4(s, 10, 1, 0, 200) && ip4(s, 11, 255, 0, 1));
    ok("ip edge", ip4(s, 10, 0, 0, 0) && ip4(s, 10, 0, 255, 255));
    ok("ip out", !ip4(s, 10, 2, 0, 0) && !ip4(s, 9, 255, 255, 255) && !ip4(s, 12, 0, 0, 0));

    geo_ip_t *any = geo_ip_new();
    geo_ip_add(any, all, 4, 0);
    geo_ip_seal(any);
    ok("ip zero prefix", ip4(any, 0, 0, 0, 0) && ip4(any, 255, 255, 255, 255));
    geo_ip_free(any);

    uint8_t v6[16] = { 0x20, 0x01, 0x0d, 0xb8 };
    ok("ip6 add", geo_ip_add(s, v6, 16, 32) == GEO_OK);
    geo_ip_seal(s);
    uint8_t in6[16] = { 0x20, 0x01, 0x0d, 0xb8, 0xff, 0xff }, out6[16] = { 0x20, 0x01, 0x0d, 0xb9 };
    ok("ip6 in", geo_ip_match(s, in6, 16));
    ok("ip6 out", !geo_ip_match(s, out6, 16));

    char path[512];
    snprintf(path, sizeof path, "%s/i.bin", dir);
    ok("ip save", geo_ip_save(s, path) == GEO_OK);
    geo_ip_t *back = NULL;
    ok("ip load", geo_ip_load(path, &back) == GEO_OK && back);
    ok("ip roundtrip", back && ip4(back, 10, 0, 1, 1) && !ip4(back, 12, 0, 0, 1) &&
       geo_ip_match(back, in6, 16) && !geo_ip_match(back, out6, 16));
    geo_ip_free(back);
    truncate(path, 20);
    ok("ip truncated refused", geo_ip_load(path, &back) == GEO_ERR_FORMAT);

    geo_ip_set_reverse(s, 1);
    ok("ip reverse", !ip4(s, 10, 0, 1, 1) && ip4(s, 12, 0, 0, 1));
    geo_ip_free(s);

    geo_ip_t *p = geo_ip_private();
    ok("private built", p != NULL);
    ok("private ranges", ip4(p, 10, 1, 2, 3) && ip4(p, 192, 168, 1, 1) &&
       ip4(p, 172, 31, 0, 1) && ip4(p, 127, 0, 0, 1) && ip4(p, 169, 254, 1, 1));
    ok("private public", !ip4(p, 8, 8, 8, 8) && !ip4(p, 172, 32, 0, 1) &&
       !ip4(p, 192, 169, 0, 1));
    uint8_t ula[16] = { 0xfd, 0x12 }, lla[16] = { 0xfe, 0x80, 1 }, pub6[16] = { 0x20, 1 };
    ok("private v6", geo_ip_match(p, ula, 16) && geo_ip_match(p, lla, 16) &&
       !geo_ip_match(p, pub6, 16));
    geo_ip_free(p);
}

static void test_tags(void) {
    char path[128];
    ok("tag ok", geo_tag_valid("cn") && geo_tag_valid("geolocation-!cn") &&
       geo_tag_valid("google@ads") && geo_tag_valid("category-ru.x"));
    ok("tag bad", !geo_tag_valid("") && !geo_tag_valid("../x") && !geo_tag_valid("a/b") &&
       !geo_tag_valid("Cn") && !geo_tag_valid("-x") && !geo_tag_valid("a..b") &&
       !geo_tag_valid(NULL) && !geo_tag_valid("a b"));
    ok("tag path", geo_tag_path("/d", GEO_KIND_SITE, "geolocation-!cn", path, sizeof path) == 0 &&
       strcmp(path, "/d/site-geolocation-~cn.bin") == 0);
    ok("tag path ip", geo_tag_path("/d", GEO_KIND_IP, "ru", path, sizeof path) == 0 &&
       strcmp(path, "/d/ip-ru.bin") == 0);
    ok("tag path short", geo_tag_path("/d", GEO_KIND_IP, "ru", path, 6) == -1);
}

static void test_extract_site(const char *dir) {
    buf_t dat = {0};
    buf_t e = {0};
    /* an unrelated top level field, then entries */
    put_num(&dat, 5, 7);
    put_str(&e, 1, "CN");
    domain(&e, 2, "baidu.com", NULL);
    domain(&e, 3, "full.cn.test", NULL);
    domain(&e, 0, "cnkw", NULL);
    domain(&e, 1, "^re.*gex$", NULL);
    domain(&e, 2, "ads.cn.test", "ads");
    put_field(&dat, 1, &e);
    e.n = 0;
    put_str(&e, 1, "SKIPPED");
    for (int i = 0; i < 50; ++i) {
        char name[32];
        snprintf(name, sizeof name, "skipped%d.test", i);
        domain(&e, 2, name, NULL);
    }
    put_field(&dat, 1, &e);
    e.n = 0;
    put_str(&e, 1, "GOOGLE");
    domain(&e, 2, "google.com", NULL);
    domain(&e, 2, "cn.google.com", "cn");
    put_field(&dat, 1, &e);
    e.n = 0;
    put_str(&e, 1, "PRIVATE");
    domain(&e, 2, "local", NULL);
    put_field(&dat, 1, &e);

    char dat_path[512];
    snprintf(dat_path, sizeof dat_path, "%s/geosite.dat", dir);
    write_file(dat_path, &dat);

    const char *specs[] = { "cn", "cn@ads", "google", "google@cn", "missing", "private" };
    uint8_t found[6];
    geo_extract_stats_t stats;
    ok("extract site", geo_dat_extract(dat_path, GEO_KIND_SITE, dir, specs, 6,
                                       found, &stats) == GEO_OK);
    ok("extract found flags", found[0] && found[1] && found[2] && found[3] &&
       !found[4] && found[5]);
    ok("extract stats", stats.entries_seen == 4 && stats.regexp_skipped == 1 &&
       stats.tags_found == 3);

    char path[512];
    geo_site_t *cn = NULL, *ads = NULL, *g = NULL, *gcn = NULL;
    geo_tag_path(dir, GEO_KIND_SITE, "cn", path, sizeof path);
    ok("cn load", geo_site_load(path, &cn) == GEO_OK);
    geo_tag_path(dir, GEO_KIND_SITE, "cn@ads", path, sizeof path);
    ok("cn ads load", geo_site_load(path, &ads) == GEO_OK);
    geo_tag_path(dir, GEO_KIND_SITE, "google", path, sizeof path);
    ok("google load", geo_site_load(path, &g) == GEO_OK);
    geo_tag_path(dir, GEO_KIND_SITE, "google@cn", path, sizeof path);
    ok("google cn load", geo_site_load(path, &gcn) == GEO_OK);
    ok("cn content", cn && geo_site_match(cn, "www.baidu.com") &&
       geo_site_match(cn, "full.cn.test") && !geo_site_match(cn, "a.full.cn.test") &&
       geo_site_match(cn, "xcnkwx.org") && geo_site_match(cn, "ads.cn.test") &&
       !geo_site_match(cn, "google.com"));
    ok("cn attr only", ads && geo_site_match(ads, "ads.cn.test") &&
       !geo_site_match(ads, "baidu.com") && geo_site_count(ads) == 1);
    ok("google content", g && geo_site_match(g, "mail.google.com") && !geo_site_match(g, "baidu.com"));
    ok("google attr", gcn && geo_site_match(gcn, "cn.google.com") &&
       !geo_site_match(gcn, "google.com"));
    geo_tag_path(dir, GEO_KIND_SITE, "missing", path, sizeof path);
    ok("missing not written", access(path, F_OK) != 0);
    geo_site_free(cn); geo_site_free(ads); geo_site_free(g); geo_site_free(gcn);

    /* a dat cut in the middle of an entry is a format error and writes nothing
       for the tags it never finished */
    dat.n -= 3;
    write_file(dat_path, &dat);
    const char *only[] = { "private" };
    geo_tag_path(dir, GEO_KIND_SITE, "private", path, sizeof path);
    unlink(path);
    uint8_t f1[1];
    ok("truncated dat refused", geo_dat_extract(dat_path, GEO_KIND_SITE, dir, only, 1,
                                                f1, NULL) == GEO_ERR_FORMAT);
    ok("truncated nothing written", access(path, F_OK) != 0);

    /* an entry length larger than the file */
    buf_t lie = {0};
    put_varint(&lie, 1 << 3 | 2);
    put_varint(&lie, 9999);
    put_str(&lie, 1, "CN");
    write_file(dat_path, &lie);
    ok("lying length refused", geo_dat_extract(dat_path, GEO_KIND_SITE, dir, only, 1,
                                               f1, NULL) == GEO_ERR_FORMAT);
    ok("missing dat", geo_dat_extract("/nonexistent/x.dat", GEO_KIND_SITE, dir, only, 1,
                                      f1, NULL) == GEO_ERR_IO);
    const char *bad[] = { "../x" };
    ok("bad spec", geo_dat_extract(dat_path, GEO_KIND_SITE, dir, bad, 1, f1, NULL) ==
       GEO_ERR_ARG);
    ok("no specs", geo_dat_extract(dat_path, GEO_KIND_SITE, dir, NULL, 0, NULL, NULL) == GEO_OK);
    free(lie.p);
    free(e.p);
    free(dat.p);
}

static void test_extract_ip(const char *dir) {
    buf_t dat = {0};
    buf_t e = {0};
    const uint8_t cn1[4] = { 1, 0, 1, 0 }, cn2[4] = { 1, 0, 2, 0 };
    uint8_t cn6[16] = { 0x24, 0x0e };
    const uint8_t us[4] = { 8, 8, 8, 0 };
    put_str(&e, 1, "CN");
    cidr(&e, cn1, 4, 24);
    cidr(&e, cn2, 4, 23);
    cidr(&e, cn6, 16, 20);
    put_field(&dat, 1, &e);
    e.n = 0;
    put_str(&e, 1, "US");
    cidr(&e, us, 4, 24);
    put_field(&dat, 1, &e);
    e.n = 0;
    put_str(&e, 1, "NOTCN");
    cidr(&e, cn1, 4, 8);
    put_num(&e, 3, 1);
    put_field(&dat, 1, &e);

    char dat_path[512];
    snprintf(dat_path, sizeof dat_path, "%s/geoip.dat", dir);
    write_file(dat_path, &dat);
    const char *specs[] = { "cn", "notcn", "ru" };
    uint8_t found[3];
    ok("extract ip", geo_dat_extract(dat_path, GEO_KIND_IP, dir, specs, 3, found, NULL) == GEO_OK);
    ok("ip found flags", found[0] && found[1] && !found[2]);
    char path[512];
    geo_ip_t *cn = NULL, *neg = NULL;
    geo_tag_path(dir, GEO_KIND_IP, "cn", path, sizeof path);
    ok("cn ip load", geo_ip_load(path, &cn) == GEO_OK);
    geo_tag_path(dir, GEO_KIND_IP, "notcn", path, sizeof path);
    ok("notcn ip load", geo_ip_load(path, &neg) == GEO_OK);
    uint8_t in6[16] = { 0x24, 0x0e, 0x0f }, out6[16] = { 0x24, 0x0e, 0x10 };
    ok("cn ip content", cn && ip4(cn, 1, 0, 1, 7) && ip4(cn, 1, 0, 3, 255) &&
       !ip4(cn, 1, 0, 4, 0) && !ip4(cn, 8, 8, 8, 8) && geo_ip_match(cn, in6, 16) &&
       !geo_ip_match(cn, out6, 16));
    ok("cn ranges merged", cn && geo_ip_count(cn) == 2);
    ok("reverse flag", neg && !ip4(neg, 1, 2, 3, 4) && ip4(neg, 9, 9, 9, 9));
    geo_ip_free(cn);
    geo_ip_free(neg);

    /* a cidr with a prefix longer than its address is dropped, not stored */
    e.n = 0;
    dat.n = 0;
    put_str(&e, 1, "BAD");
    cidr(&e, cn1, 4, 40);
    cidr(&e, cn2, 4, 24);
    put_field(&dat, 1, &e);
    write_file(dat_path, &dat);
    const char *bad[] = { "bad" };
    ok("bad prefix dropped", geo_dat_extract(dat_path, GEO_KIND_IP, dir, bad, 1, found, NULL) == GEO_OK);
    geo_tag_path(dir, GEO_KIND_IP, "bad", path, sizeof path);
    geo_ip_t *b = NULL;
    ok("bad prefix kept rest", geo_ip_load(path, &b) == GEO_OK && geo_ip_count(b) == 1);
    geo_ip_free(b);
    free(e.p);
    free(dat.p);
}

static void test_slice(const char *dir) {
    /* three entries; the slice keeps the named two byte for byte */
    buf_t dat = {0}, ru = {0}, cn = {0}, g = {0};
    const char *codes_all[3] = { "RU", "CN", "GOOGLE" };
    buf_t *entries[3] = { &ru, &cn, &g };
    for (int i = 0; i < 3; ++i) {
        put_str(entries[i], 1, codes_all[i]);
        domain(entries[i], 2, i == 0 ? "yandex.ru" : i == 1 ? "baidu.com" : "google.com", NULL);
        put_field(&dat, 1, entries[i]);
    }
    char in[256], out[256];
    snprintf(in, sizeof in, "%s/slice-in.dat", dir);
    snprintf(out, sizeof out, "%s/slice-out.dat", dir);
    write_file(in, &dat);

    const char *want[3] = { "google", "ru", "missing" };
    uint8_t found[3];
    ok("a slice is written", geo_dat_slice(in, out, want, 3, found) == GEO_OK);
    ok("found flags follow the dat", found[0] == 1 && found[1] == 1 && found[2] == 0);
    buf_t expect = {0};
    put_field(&expect, 1, &ru);
    put_field(&expect, 1, &g);
    FILE *f = fopen(out, "rb");
    uint8_t back[512];
    size_t n = f ? fread(back, 1, sizeof back, f) : 0;
    if (f) fclose(f);
    ok("the slice holds the entries of the named codes only, in dat order",
       n == expect.n && memcmp(back, expect.p, n) == 0);

    /* the slice is a dat the extractor reads like the original */
    const char *specs[2] = { "ru", "cn" };
    uint8_t seen[2];
    ok("the slice extracts", geo_dat_extract(out, GEO_KIND_SITE, dir, specs, 2, seen, NULL) == GEO_OK);
    ok("the dropped code is gone from it", seen[0] == 1 && seen[1] == 0);

    ok("a missing dat is an io error", geo_dat_slice("/nonexistent/geo.dat", out, want, 3, found) == GEO_ERR_IO);
    dat.n -= 3;
    write_file(in, &dat);
    ok("a cut dat is a format error and leaves no file",
       geo_dat_slice(in, out, want, 3, found) == GEO_ERR_FORMAT && access(out, F_OK) != 0);
    ok("bad arguments are refused", geo_dat_slice(NULL, out, want, 3, found) == GEO_ERR_ARG);
    free(dat.p); free(ru.p); free(cn.p); free(g.p); free(expect.p);
}

int main(void) {
    char dir[] = "/tmp/senko-geo-XXXXXX";
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        return 2;
    }
    test_tags();
    test_site_set(dir);
    test_ip_set(dir);
    test_extract_site(dir);
    test_extract_ip(dir);
    test_slice(dir);
    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd) != 0) return 2;
    if (failures) {
        fprintf(stderr, "%d geo checks failed\n", failures);
        return 1;
    }
    puts("all geo checks passed");
    return 0;
}
