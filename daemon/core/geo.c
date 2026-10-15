#define _POSIX_C_SOURCE 200809L

#include "geo.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define SITE_MAGIC "SGS1"
#define IP_MAGIC "SGI1"

struct geo_site {
    uint64_t *suffix;
    size_t suffix_n, suffix_cap;
    uint64_t *full;
    size_t full_n, full_cap;
    char (*keyword)[GEO_KEYWORD_LEN_MAX + 1];
    size_t keyword_n;
    int sealed;
};

typedef struct {
    uint32_t start;
    uint32_t end;
} range4_t;

typedef struct {
    uint8_t start[16];
    uint8_t end[16];
} range6_t;

struct geo_ip {
    range4_t *v4;
    size_t v4_n, v4_cap;
    range6_t *v6;
    size_t v6_n, v6_cap;
    int reverse;
    int sealed;
};

/* fnv-1a over the lower case name. a 64 bit hash makes a false hit about as
   likely as a cosmic ray at these set sizes, and keeps a set at 8 bytes a name */
static uint64_t hash_name(const char *s, size_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= (uint8_t)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a;
    uint64_t y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

/* the buffer with room for need items: the same one when it already has, a
   bigger one otherwise, NULL when memory ran out (the old buffer stays valid) */
static void *grown(void *buf, size_t *cap, size_t need, size_t item) {
    if (need <= *cap) return buf;
    size_t next = *cap ? *cap : 64;
    while (next < need) next *= 2;
    void *fresh = realloc(buf, next * item);
    if (fresh) *cap = next;
    return fresh;
}

geo_site_t *geo_site_new(void) {
    return (geo_site_t *)calloc(1, sizeof(geo_site_t));
}

void geo_site_free(geo_site_t *set) {
    if (!set) return;
    free(set->suffix);
    free(set->full);
    free(set->keyword);
    free(set);
}

size_t geo_site_count(const geo_site_t *set) {
    return set ? set->suffix_n + set->full_n + set->keyword_n : 0;
}

geo_status_t geo_site_add(geo_site_t *set, geo_site_kind_t kind,
                          const char *value, size_t len) {
    char norm[256];
    if (!set || !value) return GEO_ERR_ARG;
    while (len > 0 && value[len - 1] == '.') --len;
    while (len > 0 && value[0] == '.') {
        ++value;
        --len;
    }
    if (len == 0 || len >= sizeof norm) return GEO_ERR_ARG;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (c < 0x21 || c >= 0x7f) return GEO_ERR_ARG;
        norm[i] = (char)tolower(c);
    }
    norm[len] = '\0';
    if (geo_site_count(set) >= GEO_SITE_MAX_ENTRIES) return GEO_ERR_TOOBIG;
    set->sealed = 0;
    if (kind == GEO_SITE_KEYWORD) {
        if (len > GEO_KEYWORD_LEN_MAX || set->keyword_n >= GEO_KEYWORD_MAX)
            return GEO_ERR_TOOBIG;
        char (*grown)[GEO_KEYWORD_LEN_MAX + 1] = realloc(
            set->keyword, (set->keyword_n + 1) * sizeof set->keyword[0]);
        if (!grown) return GEO_ERR_MEMORY;
        set->keyword = grown;
        memcpy(set->keyword[set->keyword_n], norm, len + 1);
        ++set->keyword_n;
        return GEO_OK;
    }
    if (kind == GEO_SITE_FULL) {
        {
        void *room = grown(set->full, &set->full_cap, set->full_n + 1, sizeof *set->full);
        if (!room) return GEO_ERR_MEMORY;
        set->full = room;
    }
        set->full[set->full_n++] = hash_name(norm, len);
        return GEO_OK;
    }
    {
        void *room = grown(set->suffix, &set->suffix_cap, set->suffix_n + 1, sizeof *set->suffix);
        if (!room) return GEO_ERR_MEMORY;
        set->suffix = room;
    }
    set->suffix[set->suffix_n++] = hash_name(norm, len);
    return GEO_OK;
}

static size_t dedupe_u64(uint64_t *v, size_t n) {
    if (n < 2) return n;
    qsort(v, n, sizeof *v, cmp_u64);
    size_t w = 1;
    for (size_t i = 1; i < n; ++i)
        if (v[i] != v[w - 1]) v[w++] = v[i];
    return w;
}

geo_status_t geo_site_seal(geo_site_t *set) {
    if (!set) return GEO_ERR_ARG;
    set->suffix_n = dedupe_u64(set->suffix, set->suffix_n);
    set->full_n = dedupe_u64(set->full, set->full_n);
    set->sealed = 1;
    return GEO_OK;
}

static int has_hash(const uint64_t *v, size_t n, uint64_t h) {
    return n && bsearch(&h, v, n, sizeof *v, cmp_u64) != NULL;
}

int geo_site_match(const geo_site_t *set, const char *domain) {
    if (!set || !domain || !set->sealed) return 0;
    size_t len = strlen(domain);
    if (len == 0) return 0;
    if (has_hash(set->full, set->full_n, hash_name(domain, len))) return 1;
    /* every label boundary is a candidate suffix, the whole name included */
    for (size_t i = 0; i < len; ++i) {
        if (i != 0 && domain[i - 1] != '.') continue;
        if (has_hash(set->suffix, set->suffix_n, hash_name(domain + i, len - i)))
            return 1;
    }
    for (size_t i = 0; i < set->keyword_n; ++i)
        if (strstr(domain, set->keyword[i])) return 1;
    return 0;
}

geo_ip_t *geo_ip_new(void) {
    return (geo_ip_t *)calloc(1, sizeof(geo_ip_t));
}

void geo_ip_free(geo_ip_t *set) {
    if (!set) return;
    free(set->v4);
    free(set->v6);
    free(set);
}

size_t geo_ip_count(const geo_ip_t *set) {
    return set ? set->v4_n + set->v6_n : 0;
}

void geo_ip_set_reverse(geo_ip_t *set, int reverse) {
    if (set) set->reverse = reverse != 0;
}

geo_status_t geo_ip_add(geo_ip_t *set, const uint8_t *address, uint8_t address_len,
                        uint8_t prefix) {
    if (!set || !address) return GEO_ERR_ARG;
    if (geo_ip_count(set) >= GEO_IP_MAX_RANGES) return GEO_ERR_TOOBIG;
    set->sealed = 0;
    if (address_len == 4) {
        if (prefix > 32) return GEO_ERR_ARG;
        uint32_t ip = (uint32_t)address[0] << 24 | (uint32_t)address[1] << 16 |
                      (uint32_t)address[2] << 8 | address[3];
        uint32_t mask = prefix == 0 ? 0 : 0xffffffffu << (32 - prefix);
        {
        void *room = grown(set->v4, &set->v4_cap, set->v4_n + 1, sizeof *set->v4);
        if (!room) return GEO_ERR_MEMORY;
        set->v4 = room;
    }
        set->v4[set->v4_n].start = ip & mask;
        set->v4[set->v4_n].end = (ip & mask) | ~mask;
        ++set->v4_n;
        return GEO_OK;
    }
    if (address_len == 16) {
        if (prefix > 128) return GEO_ERR_ARG;
        range6_t r;
        memcpy(r.start, address, 16);
        memcpy(r.end, address, 16);
        for (unsigned bit = prefix; bit < 128; ++bit) {
            r.start[bit / 8] &= (uint8_t)~(0x80u >> (bit % 8));
            r.end[bit / 8] |= (uint8_t)(0x80u >> (bit % 8));
        }
        {
        void *room = grown(set->v6, &set->v6_cap, set->v6_n + 1, sizeof *set->v6);
        if (!room) return GEO_ERR_MEMORY;
        set->v6 = room;
    }
        set->v6[set->v6_n++] = r;
        return GEO_OK;
    }
    return GEO_ERR_ARG;
}

static int cmp_r4(const void *a, const void *b) {
    const range4_t *x = a, *y = b;
    return x->start < y->start ? -1 : x->start > y->start;
}

static int cmp_r6(const void *a, const void *b) {
    return memcmp(((const range6_t *)a)->start, ((const range6_t *)b)->start, 16);
}

/* adds one to a big endian address; 1 when it wrapped past the last address */
static int inc16(uint8_t *a) {
    for (int i = 15; i >= 0; --i)
        if (++a[i] != 0) return 0;
    return 1;
}

geo_status_t geo_ip_seal(geo_ip_t *set) {
    if (!set) return GEO_ERR_ARG;
    if (set->v4_n > 1) {
        qsort(set->v4, set->v4_n, sizeof *set->v4, cmp_r4);
        size_t w = 0;
        for (size_t i = 1; i < set->v4_n; ++i) {
            range4_t *cur = &set->v4[w];
            /* touching ranges merge too; the +1 cannot wrap because a range that
               ends at 255.255.255.255 has nothing after it to touch */
            if (set->v4[i].start <= cur->end ||
                (cur->end != 0xffffffffu && set->v4[i].start == cur->end + 1)) {
                if (set->v4[i].end > cur->end) cur->end = set->v4[i].end;
            } else {
                set->v4[++w] = set->v4[i];
            }
        }
        set->v4_n = w + 1;
    }
    if (set->v6_n > 1) {
        qsort(set->v6, set->v6_n, sizeof *set->v6, cmp_r6);
        size_t w = 0;
        for (size_t i = 1; i < set->v6_n; ++i) {
            range6_t *cur = &set->v6[w];
            uint8_t after[16];
            memcpy(after, cur->end, 16);
            int wrapped = inc16(after);
            if (memcmp(set->v6[i].start, cur->end, 16) <= 0 ||
                (!wrapped && memcmp(set->v6[i].start, after, 16) == 0)) {
                if (memcmp(set->v6[i].end, cur->end, 16) > 0)
                    memcpy(cur->end, set->v6[i].end, 16);
            } else {
                set->v6[++w] = set->v6[i];
            }
        }
        set->v6_n = w + 1;
    }
    set->sealed = 1;
    return GEO_OK;
}

int geo_ip_match(const geo_ip_t *set, const uint8_t *address, uint8_t address_len) {
    int hit = 0;
    if (!set || !address || !set->sealed) return 0;
    if (address_len == 4) {
        uint32_t ip = (uint32_t)address[0] << 24 | (uint32_t)address[1] << 16 |
                      (uint32_t)address[2] << 8 | address[3];
        size_t lo = 0, hi = set->v4_n;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (set->v4[mid].start <= ip) lo = mid + 1;
            else hi = mid;
        }
        hit = lo > 0 && ip <= set->v4[lo - 1].end;
    } else if (address_len == 16) {
        size_t lo = 0, hi = set->v6_n;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (memcmp(set->v6[mid].start, address, 16) <= 0) lo = mid + 1;
            else hi = mid;
        }
        hit = lo > 0 && memcmp(address, set->v6[lo - 1].end, 16) <= 0;
    } else {
        return 0;
    }
    return set->reverse ? !hit : hit;
}

geo_ip_t *geo_ip_private(void) {
    static const struct { uint8_t a[4]; uint8_t p; } v4[] = {
        {{0, 0, 0, 0}, 8}, {{10, 0, 0, 0}, 8}, {{100, 64, 0, 0}, 10},
        {{127, 0, 0, 0}, 8}, {{169, 254, 0, 0}, 16}, {{172, 16, 0, 0}, 12},
        {{192, 0, 0, 0}, 24}, {{192, 0, 2, 0}, 24}, {{192, 88, 99, 0}, 24},
        {{192, 168, 0, 0}, 16}, {{198, 18, 0, 0}, 15}, {{198, 51, 100, 0}, 24},
        {{203, 0, 113, 0}, 24}, {{224, 0, 0, 0}, 4}, {{240, 0, 0, 0}, 4}
    };
    static const uint8_t loopback6[16] = { [15] = 1 };
    static const uint8_t unique6[16] = { 0xfc };
    static const uint8_t link6[16] = { 0xfe, 0x80 };
    geo_ip_t *set = geo_ip_new();
    if (!set) return NULL;
    for (size_t i = 0; i < sizeof v4 / sizeof v4[0]; ++i)
        if (geo_ip_add(set, v4[i].a, 4, v4[i].p) != GEO_OK) goto fail;
    if (geo_ip_add(set, loopback6, 16, 128) != GEO_OK ||
        geo_ip_add(set, unique6, 16, 7) != GEO_OK ||
        geo_ip_add(set, link6, 16, 10) != GEO_OK ||
        geo_ip_seal(set) != GEO_OK) goto fail;
    return set;
fail:
    geo_ip_free(set);
    return NULL;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static uint64_t get64(const uint8_t *p) {
    return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32;
}

/* a set goes to disk through a temporary name, so a crash or a full disk never
   leaves a half written file under the name a rule loads */
static geo_status_t write_atomic(const char *path, const uint8_t *data, size_t len) {
    char tmp[512];
    int n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof tmp) return GEO_ERR_ARG;
    FILE *f = fopen(tmp, "wb");
    if (!f) return GEO_ERR_IO;
    int ok = fwrite(data, 1, len, f) == len;
    ok = fflush(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) {
        (void)unlink(tmp);
        return GEO_ERR_IO;
    }
    return GEO_OK;
}

static geo_status_t read_all(const char *path, size_t max, uint8_t **out, size_t *len) {
    *out = NULL;
    *len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return GEO_ERR_IO;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return GEO_ERR_IO; }
    long size = ftell(f);
    if (size < 0 || (unsigned long)size > max) { fclose(f); return GEO_ERR_TOOBIG; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc(size ? (size_t)size : 1);
    if (!buf) { fclose(f); return GEO_ERR_MEMORY; }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return GEO_ERR_IO;
    }
    fclose(f);
    *out = buf;
    *len = (size_t)size;
    return GEO_OK;
}

geo_status_t geo_site_save(const geo_site_t *set, const char *path) {
    if (!set || !path || !set->sealed) return GEO_ERR_ARG;
    size_t len = 20 + (set->suffix_n + set->full_n) * 8;
    for (size_t i = 0; i < set->keyword_n; ++i) len += 1 + strlen(set->keyword[i]);
    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf) return GEO_ERR_MEMORY;
    uint8_t *p = buf;
    memcpy(p, SITE_MAGIC, 4);
    put32(p + 4, (uint32_t)set->suffix_n);
    put32(p + 8, (uint32_t)set->full_n);
    put32(p + 12, (uint32_t)set->keyword_n);
    put32(p + 16, 0);
    p += 20;
    for (size_t i = 0; i < set->suffix_n; ++i, p += 8) put64(p, set->suffix[i]);
    for (size_t i = 0; i < set->full_n; ++i, p += 8) put64(p, set->full[i]);
    for (size_t i = 0; i < set->keyword_n; ++i) {
        size_t kl = strlen(set->keyword[i]);
        *p++ = (uint8_t)kl;
        memcpy(p, set->keyword[i], kl);
        p += kl;
    }
    geo_status_t r = write_atomic(path, buf, len);
    free(buf);
    return r;
}

geo_status_t geo_site_load(const char *path, geo_site_t **out) {
    uint8_t *buf;
    size_t len;
    if (!path || !out) return GEO_ERR_ARG;
    *out = NULL;
    geo_status_t r = read_all(path, 20u + GEO_SITE_MAX_ENTRIES * 8u * 2u +
                              GEO_KEYWORD_MAX * (GEO_KEYWORD_LEN_MAX + 1u),
                              &buf, &len);
    if (r != GEO_OK) return r;
    geo_site_t *set = NULL;
    if (len < 20 || memcmp(buf, SITE_MAGIC, 4) != 0) { r = GEO_ERR_FORMAT; goto done; }
    uint32_t ns = get32(buf + 4), nf = get32(buf + 8), nk = get32(buf + 12);
    if (ns > GEO_SITE_MAX_ENTRIES || nf > GEO_SITE_MAX_ENTRIES ||
        nk > GEO_KEYWORD_MAX || 20 + ((size_t)ns + nf) * 8 > len) {
        r = GEO_ERR_FORMAT;
        goto done;
    }
    set = geo_site_new();
    if (!set) { r = GEO_ERR_MEMORY; goto done; }
    const uint8_t *p = buf + 20;
    if (ns) {
        set->suffix = grown(NULL, &set->suffix_cap, ns, sizeof *set->suffix);
        if (!set->suffix) { r = GEO_ERR_MEMORY; goto done; }
    }
    if (nf) {
        set->full = grown(NULL, &set->full_cap, nf, sizeof *set->full);
        if (!set->full) { r = GEO_ERR_MEMORY; goto done; }
    }
    for (uint32_t i = 0; i < ns; ++i, p += 8) set->suffix[i] = get64(p);
    for (uint32_t i = 0; i < nf; ++i, p += 8) set->full[i] = get64(p);
    set->suffix_n = ns;
    set->full_n = nf;
    for (uint32_t i = 0; i < nk; ++i) {
        size_t left = (size_t)(buf + len - p);
        if (left < 1 || p[0] == 0 || p[0] > GEO_KEYWORD_LEN_MAX || left < 1u + p[0]) {
            r = GEO_ERR_FORMAT;
            goto done;
        }
        if (geo_site_add(set, GEO_SITE_KEYWORD, (const char *)p + 1, p[0]) != GEO_OK) {
            r = GEO_ERR_FORMAT;
            goto done;
        }
        p += 1u + p[0];
    }
    /* a saved set is sorted, but a damaged file must not turn the binary search
       into a wrong answer, so it is verified instead of trusted */
    for (size_t i = 1; i < set->suffix_n; ++i)
        if (set->suffix[i - 1] >= set->suffix[i]) { r = GEO_ERR_FORMAT; goto done; }
    for (size_t i = 1; i < set->full_n; ++i)
        if (set->full[i - 1] >= set->full[i]) { r = GEO_ERR_FORMAT; goto done; }
    set->sealed = 1;
    *out = set;
    set = NULL;
    r = GEO_OK;
done:
    geo_site_free(set);
    free(buf);
    return r;
}

geo_status_t geo_ip_save(const geo_ip_t *set, const char *path) {
    if (!set || !path || !set->sealed) return GEO_ERR_ARG;
    size_t len = 16 + set->v4_n * 8 + set->v6_n * 32;
    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf) return GEO_ERR_MEMORY;
    memcpy(buf, IP_MAGIC, 4);
    buf[4] = (uint8_t)set->reverse;
    buf[5] = buf[6] = buf[7] = 0;
    put32(buf + 8, (uint32_t)set->v4_n);
    put32(buf + 12, (uint32_t)set->v6_n);
    uint8_t *p = buf + 16;
    for (size_t i = 0; i < set->v4_n; ++i, p += 8) {
        put32(p, set->v4[i].start);
        put32(p + 4, set->v4[i].end);
    }
    for (size_t i = 0; i < set->v6_n; ++i, p += 32) {
        memcpy(p, set->v6[i].start, 16);
        memcpy(p + 16, set->v6[i].end, 16);
    }
    geo_status_t r = write_atomic(path, buf, len);
    free(buf);
    return r;
}

geo_status_t geo_ip_load(const char *path, geo_ip_t **out) {
    uint8_t *buf;
    size_t len;
    if (!path || !out) return GEO_ERR_ARG;
    *out = NULL;
    geo_status_t r = read_all(path, 16u + GEO_IP_MAX_RANGES * 32u, &buf, &len);
    if (r != GEO_OK) return r;
    geo_ip_t *set = NULL;
    if (len < 16 || memcmp(buf, IP_MAGIC, 4) != 0) { r = GEO_ERR_FORMAT; goto done; }
    uint32_t n4 = get32(buf + 8), n6 = get32(buf + 12);
    if (n4 > GEO_IP_MAX_RANGES || n6 > GEO_IP_MAX_RANGES ||
        16 + (size_t)n4 * 8 + (size_t)n6 * 32 != len) {
        r = GEO_ERR_FORMAT;
        goto done;
    }
    set = geo_ip_new();
    if (!set) { r = GEO_ERR_MEMORY; goto done; }
    set->reverse = buf[4] != 0;
    if (n4) {
        set->v4 = grown(NULL, &set->v4_cap, n4, sizeof *set->v4);
        if (!set->v4) { r = GEO_ERR_MEMORY; goto done; }
    }
    if (n6) {
        set->v6 = grown(NULL, &set->v6_cap, n6, sizeof *set->v6);
        if (!set->v6) { r = GEO_ERR_MEMORY; goto done; }
    }
    const uint8_t *p = buf + 16;
    for (uint32_t i = 0; i < n4; ++i, p += 8) {
        set->v4[i].start = get32(p);
        set->v4[i].end = get32(p + 4);
        if (set->v4[i].start > set->v4[i].end ||
            (i && set->v4[i].start <= set->v4[i - 1].end)) { r = GEO_ERR_FORMAT; goto done; }
    }
    for (uint32_t i = 0; i < n6; ++i, p += 32) {
        memcpy(set->v6[i].start, p, 16);
        memcpy(set->v6[i].end, p + 16, 16);
        if (memcmp(set->v6[i].start, set->v6[i].end, 16) > 0 ||
            (i && memcmp(set->v6[i].start, set->v6[i - 1].end, 16) <= 0)) { r = GEO_ERR_FORMAT; goto done; }
    }
    set->v4_n = n4;
    set->v6_n = n6;
    set->sealed = 1;
    *out = set;
    set = NULL;
    r = GEO_OK;
done:
    geo_ip_free(set);
    free(buf);
    return r;
}

int geo_tag_valid(const char *tag) {
    if (!tag || !isalnum((unsigned char)tag[0])) return 0;
    size_t n = 0;
    for (; tag[n]; ++n) {
        unsigned char c = (unsigned char)tag[n];
        if (!(islower(c) || isdigit(c) || c == '.' || c == '_' || c == '@' ||
              c == '!' || c == '-')) return 0;
    }
    return n > 0 && n < GEO_TAG_MAX && strstr(tag, "..") == NULL;
}

int geo_tag_path(const char *dir, geo_kind_t kind, const char *tag,
                 char *out, size_t cap) {
    char name[GEO_TAG_MAX];
    if (!dir || !out || !geo_tag_valid(tag)) return -1;
    /* '!' is legal in a geo tag (geolocation-!cn) but not worth carrying into a
       file name */
    size_t i = 0;
    for (; tag[i]; ++i) name[i] = tag[i] == '!' ? '~' : tag[i];
    name[i] = '\0';
    int n = snprintf(out, cap, "%s/%s-%s.bin", dir,
                     kind == GEO_KIND_SITE ? "site" : "ip", name);
    return n > 0 && (size_t)n < cap ? 0 : -1;
}

/* the dat is protobuf. the reader counts down the bytes left in the message it
   is inside, so a length field that lies can never read past its own message */
typedef struct {
    FILE *f;
    uint64_t left;
    int bad;
} reader_t;

static int rd_byte(reader_t *r) {
    if (r->left == 0) { r->bad = 1; return -1; }
    int c = fgetc(r->f);
    if (c == EOF) { r->bad = 1; return -1; }
    --r->left;
    return c;
}

static int rd_varint(reader_t *r, uint64_t *out) {
    uint64_t v = 0;
    for (unsigned shift = 0; shift < 70; shift += 7) {
        int c = rd_byte(r);
        if (c < 0) return -1;
        v |= (uint64_t)(c & 0x7f) << shift;
        if (!(c & 0x80)) {
            *out = v;
            return 0;
        }
    }
    r->bad = 1;
    return -1;
}

static int rd_skip(reader_t *r, uint64_t n) {
    if (n > r->left) { r->bad = 1; return -1; }
    if (n && fseeko(r->f, (off_t)n, SEEK_CUR) != 0) { r->bad = 1; return -1; }
    r->left -= n;
    return 0;
}

static int rd_bytes(reader_t *r, uint8_t *buf, uint64_t n) {
    if (n > r->left) { r->bad = 1; return -1; }
    if (n && fread(buf, 1, (size_t)n, r->f) != (size_t)n) { r->bad = 1; return -1; }
    r->left -= n;
    return 0;
}

static int rd_skip_field(reader_t *r, unsigned wire) {
    uint64_t v;
    switch (wire) {
        case 0: return rd_varint(r, &v);
        case 1: return rd_skip(r, 8);
        case 2:
            if (rd_varint(r, &v) != 0) return -1;
            return rd_skip(r, v);
        case 5: return rd_skip(r, 4);
        default: r->bad = 1; return -1;
    }
}

typedef struct {
    char tag[GEO_TAG_MAX];
    char attr[GEO_TAG_MAX];
    geo_site_t *site;
    geo_ip_t *ip;
    int seen;
} want_t;

#define DOMAIN_MSG_MAX 1024u
#define CIDR_MSG_MAX 64u
#define CODE_MAX 128u

static int attr_present(const uint8_t *msg, size_t len, const char *attr) {
    /* attributes are field 3 messages of {key=1 string, bool=2}. a domain holds
       a few at most, so the whole message is walked once per wanted attribute */
    size_t i = 0;
    while (i < len) {
        uint64_t key = 0;
        unsigned shift = 0;
        while (i < len && shift < 63) {
            key |= (uint64_t)(msg[i] & 0x7f) << shift;
            if (!(msg[i++] & 0x80)) break;
            shift += 7;
        }
        unsigned field = (unsigned)(key >> 3), wire = (unsigned)(key & 7);
        if (wire == 2) {
            uint64_t l = 0;
            shift = 0;
            while (i < len && shift < 63) {
                l |= (uint64_t)(msg[i] & 0x7f) << shift;
                if (!(msg[i++] & 0x80)) break;
                shift += 7;
            }
            if (l > len - i) return 0;
            if (field == 3) {
                const uint8_t *a = msg + i;
                size_t al = (size_t)l;
                if (al >= 2 && a[0] == 0x0a && a[1] <= al - 2 &&
                    a[1] == strlen(attr) && memcmp(a + 2, attr, a[1]) == 0)
                    return 1;
            }
            i += (size_t)l;
        } else if (wire == 0) {
            while (i < len && (msg[i++] & 0x80)) {}
        } else if (wire == 1) {
            i += 8;
        } else if (wire == 5) {
            i += 4;
        } else {
            return 0;
        }
    }
    return 0;
}

static geo_status_t take_domain(reader_t *r, want_t *w, size_t nw,
                                geo_extract_stats_t *stats) {
    uint64_t len;
    uint8_t msg[DOMAIN_MSG_MAX];
    if (rd_varint(r, &len) != 0) return GEO_ERR_FORMAT;
    if (len > DOMAIN_MSG_MAX) return rd_skip(r, len) == 0 ? GEO_OK : GEO_ERR_FORMAT;
    if (rd_bytes(r, msg, len) != 0) return GEO_ERR_FORMAT;
    uint64_t type = 0;
    const uint8_t *value = NULL;
    size_t value_len = 0;
    size_t i = 0;
    while (i < len) {
        unsigned key = msg[i++];
        unsigned field = key >> 3, wire = key & 7;
        if (key & 0x80) return GEO_ERR_FORMAT;
        if (wire == 0) {
            uint64_t v = 0;
            unsigned shift = 0;
            while (i < len && shift < 63) {
                v |= (uint64_t)(msg[i] & 0x7f) << shift;
                if (!(msg[i++] & 0x80)) break;
                shift += 7;
            }
            if (field == 1) type = v;
        } else if (wire == 2) {
            uint64_t l = 0;
            unsigned shift = 0;
            while (i < len && shift < 63) {
                l |= (uint64_t)(msg[i] & 0x7f) << shift;
                if (!(msg[i++] & 0x80)) break;
                shift += 7;
            }
            if (l > len - i) return GEO_ERR_FORMAT;
            if (field == 2) {
                value = msg + i;
                value_len = (size_t)l;
            }
            i += (size_t)l;
        } else if (wire == 1) {
            i += 8;
        } else if (wire == 5) {
            i += 4;
        } else {
            return GEO_ERR_FORMAT;
        }
    }
    if (!value || value_len == 0) return GEO_OK;
    if (type == 1) {
        ++stats->regexp_skipped;
        return GEO_OK;
    }
    geo_site_kind_t kind = type == 0 ? GEO_SITE_KEYWORD
                         : type == 3 ? GEO_SITE_FULL : GEO_SITE_SUFFIX;
    if (type > 3) return GEO_OK;
    for (size_t k = 0; k < nw; ++k) {
        if (w[k].seen != 2) continue;
        if (!w[k].site) continue;
        if (w[k].attr[0] && !attr_present(msg, len, w[k].attr)) continue;
        geo_status_t s = geo_site_add(w[k].site, kind, (const char *)value, value_len);
        /* a name the set refuses (an odd byte, a keyword past the cap) is left out
           of that set, the rest of the entry still counts */
        if (s == GEO_ERR_MEMORY || s == GEO_ERR_TOOBIG) return s;
    }
    return GEO_OK;
}

static geo_status_t take_cidr(reader_t *r, want_t *w, size_t nw) {
    uint64_t len;
    uint8_t msg[CIDR_MSG_MAX];
    if (rd_varint(r, &len) != 0) return GEO_ERR_FORMAT;
    if (len > CIDR_MSG_MAX) return GEO_ERR_FORMAT;
    if (rd_bytes(r, msg, len) != 0) return GEO_ERR_FORMAT;
    const uint8_t *addr = NULL;
    size_t addr_len = 0;
    uint64_t prefix = 0;
    size_t i = 0;
    while (i < len) {
        unsigned key = msg[i++];
        unsigned field = key >> 3, wire = key & 7;
        if (key & 0x80) return GEO_ERR_FORMAT;
        if (wire == 0) {
            uint64_t v = 0;
            unsigned shift = 0;
            while (i < len && shift < 63) {
                v |= (uint64_t)(msg[i] & 0x7f) << shift;
                if (!(msg[i++] & 0x80)) break;
                shift += 7;
            }
            if (field == 2) prefix = v;
        } else if (wire == 2) {
            uint64_t l = 0;
            unsigned shift = 0;
            while (i < len && shift < 63) {
                l |= (uint64_t)(msg[i] & 0x7f) << shift;
                if (!(msg[i++] & 0x80)) break;
                shift += 7;
            }
            if (l > len - i) return GEO_ERR_FORMAT;
            if (field == 1) {
                addr = msg + i;
                addr_len = (size_t)l;
            }
            i += (size_t)l;
        } else {
            return GEO_ERR_FORMAT;
        }
    }
    if (!addr || (addr_len != 4 && addr_len != 16) || prefix > addr_len * 8)
        return GEO_OK;
    for (size_t k = 0; k < nw; ++k) {
        if (w[k].seen != 2) continue;
        if (!w[k].ip) continue;
        geo_status_t s = geo_ip_add(w[k].ip, addr, (uint8_t)addr_len, (uint8_t)prefix);
        if (s == GEO_ERR_MEMORY || s == GEO_ERR_TOOBIG) return s;
    }
    return GEO_OK;
}

static geo_status_t save_wants(want_t *w, size_t nw, geo_kind_t kind, const char *dir,
                               const char *const *specs, uint8_t *found) {
    for (size_t k = 0; k < nw; ++k) {
        if (!w[k].seen) continue;
        char path[512];
        if (geo_tag_path(dir, kind, specs[k], path, sizeof path) != 0)
            return GEO_ERR_ARG;
        geo_status_t s;
        if (kind == GEO_KIND_SITE) {
            if ((s = geo_site_seal(w[k].site)) != GEO_OK) return s;
            s = geo_site_save(w[k].site, path);
        } else {
            if ((s = geo_ip_seal(w[k].ip)) != GEO_OK) return s;
            s = geo_ip_save(w[k].ip, path);
        }
        if (s != GEO_OK) return s;
        found[k] = 1;
    }
    return GEO_OK;
}

geo_status_t geo_dat_extract(const char *dat_path, geo_kind_t kind, const char *dir,
                             const char *const *specs, size_t spec_count,
                             uint8_t *found, geo_extract_stats_t *stats) {
    geo_extract_stats_t local;
    if (!stats) stats = &local;
    memset(stats, 0, sizeof *stats);
    if (!dat_path || !dir || (spec_count && (!specs || !found)) ||
        spec_count > 256)
        return GEO_ERR_ARG;
    for (size_t i = 0; i < spec_count; ++i) found[i] = 0;
    if (spec_count == 0) return GEO_OK;

    want_t *w = (want_t *)calloc(spec_count, sizeof *w);
    if (!w) return GEO_ERR_MEMORY;
    geo_status_t result = GEO_OK;
    for (size_t k = 0; k < spec_count && result == GEO_OK; ++k) {
        if (!geo_tag_valid(specs[k])) { result = GEO_ERR_ARG; break; }
        const char *at = strchr(specs[k], '@');
        size_t tl = at ? (size_t)(at - specs[k]) : strlen(specs[k]);
        memcpy(w[k].tag, specs[k], tl);
        w[k].tag[tl] = '\0';
        if (at) snprintf(w[k].attr, sizeof w[k].attr, "%s", at + 1);
        if (!w[k].tag[0] || (at && !w[k].attr[0])) { result = GEO_ERR_ARG; break; }
        if (kind == GEO_KIND_SITE) w[k].site = geo_site_new();
        else w[k].ip = geo_ip_new();
        if (!w[k].site && !w[k].ip) result = GEO_ERR_MEMORY;
    }

    FILE *f = result == GEO_OK ? fopen(dat_path, "rb") : NULL;
    if (result == GEO_OK && !f) result = GEO_ERR_IO;
    if (result == GEO_OK) {
        reader_t r;
        r.f = f;
        r.bad = 0;
        if (fseeko(f, 0, SEEK_END) != 0) result = GEO_ERR_IO;
        off_t size = result == GEO_OK ? ftello(f) : 0;
        if (result == GEO_OK && (size < 0 || fseeko(f, 0, SEEK_SET) != 0))
            result = GEO_ERR_IO;
        r.left = size > 0 ? (uint64_t)size : 0;
        while (result == GEO_OK && r.left > 0) {
            uint64_t key, len;
            if (rd_varint(&r, &key) != 0) { result = GEO_ERR_FORMAT; break; }
            unsigned field = (unsigned)(key >> 3), wire = (unsigned)(key & 7);
            if (field != 1 || wire != 2) {
                if (rd_skip_field(&r, wire) != 0) result = GEO_ERR_FORMAT;
                continue;
            }
            if (rd_varint(&r, &len) != 0 || len > r.left) { result = GEO_ERR_FORMAT; break; }
            ++stats->entries_seen;
            uint64_t after = r.left - len;
            r.left = len;
            /* go reads the code field first; an entry that does not is skipped, a
               file from another writer then loses that entry instead of failing */
            uint8_t code[CODE_MAX + 1];
            size_t matched_first = (size_t)-1;
            int have_code = 0;
            if (r.left > 0) {
                uint64_t ek, cl;
                if (rd_varint(&r, &ek) == 0 && ek == 0x0a && rd_varint(&r, &cl) == 0 &&
                    cl <= CODE_MAX && rd_bytes(&r, code, cl) == 0) {
                    code[cl] = '\0';
                    have_code = 1;
                } else if (r.bad) {
                    result = GEO_ERR_FORMAT;
                }
            }
            if (result == GEO_OK && have_code) {
                for (size_t k = 0; k < spec_count; ++k) {
                    if (strcasecmp((const char *)code, w[k].tag) != 0) continue;
                    if (matched_first == (size_t)-1) matched_first = k;
                    w[k].seen = 2;
                }
            }
            if (result == GEO_OK && matched_first != (size_t)-1) {
                ++stats->tags_found;
                while (result == GEO_OK && r.left > 0) {
                    uint64_t ek;
                    if (rd_varint(&r, &ek) != 0) { result = GEO_ERR_FORMAT; break; }
                    unsigned ef = (unsigned)(ek >> 3), ew = (unsigned)(ek & 7);
                    if (ef == 2 && ew == 2) {
                        result = kind == GEO_KIND_SITE
                            ? take_domain(&r, w, spec_count, stats)
                            : take_cidr(&r, w, spec_count);
                    } else if (kind == GEO_KIND_IP && ef == 3 && ew == 0) {
                        uint64_t rev;
                        if (rd_varint(&r, &rev) != 0) { result = GEO_ERR_FORMAT; break; }
                        for (size_t k = 0; k < spec_count; ++k)
                            if (w[k].seen == 2) geo_ip_set_reverse(w[k].ip, rev != 0);
                    } else if (rd_skip_field(&r, ew) != 0) {
                        result = GEO_ERR_FORMAT;
                    }
                }
                for (size_t k = 0; k < spec_count; ++k)
                    if (w[k].seen == 2) w[k].seen = 1;
            }
            if (result != GEO_OK) break;
            if (r.left > 0 && rd_skip(&r, r.left) != 0) { result = GEO_ERR_FORMAT; break; }
            r.left = after;
        }
        fclose(f);
    }
    if (result == GEO_OK)
        result = save_wants(w, spec_count, kind, dir, specs, found);
    for (size_t k = 0; k < spec_count; ++k) {
        geo_site_free(w[k].site);
        geo_ip_free(w[k].ip);
    }
    free(w);
    return result;
}

static int put_varint(FILE *f, uint64_t v) {
    uint8_t buf[10];
    size_t n = 0;
    do {
        uint8_t b = v & 0x7f;
        v >>= 7;
        buf[n++] = v ? (uint8_t)(b | 0x80) : b;
    } while (v);
    return fwrite(buf, 1, n, f) == n ? 0 : -1;
}

geo_status_t geo_dat_slice(const char *dat_path, const char *out_path,
                           const char *const *codes, size_t code_count, uint8_t *found) {
    if (!dat_path || !out_path || (code_count && (!codes || !found)) || code_count > 256)
        return GEO_ERR_ARG;
    for (size_t i = 0; i < code_count; ++i) found[i] = 0;
    FILE *in = fopen(dat_path, "rb");
    if (!in) return GEO_ERR_IO;
    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fclose(in);
        return GEO_ERR_IO;
    }
    geo_status_t result = GEO_OK;
    reader_t r;
    r.f = in;
    r.bad = 0;
    off_t size = fseeko(in, 0, SEEK_END) == 0 ? ftello(in) : -1;
    if (size < 0 || fseeko(in, 0, SEEK_SET) != 0) result = GEO_ERR_IO;
    r.left = size > 0 ? (uint64_t)size : 0;
    while (result == GEO_OK && r.left > 0) {
        uint64_t key, len;
        if (rd_varint(&r, &key) != 0) { result = GEO_ERR_FORMAT; break; }
        if ((key >> 3) != 1 || (key & 7) != 2) {
            if (rd_skip_field(&r, (unsigned)(key & 7)) != 0) result = GEO_ERR_FORMAT;
            continue;
        }
        if (rd_varint(&r, &len) != 0 || len > r.left) { result = GEO_ERR_FORMAT; break; }
        uint64_t after = r.left - len;
        r.left = len;
        uint8_t code[CODE_MAX + 1];
        uint64_t ek = 0, cl = 0;
        size_t hit = (size_t)-1;
        /* an entry that does not start with its code is skipped, like go does */
        if (len >= 2 && rd_varint(&r, &ek) == 0 && ek == 0x0a && rd_varint(&r, &cl) == 0 &&
            cl <= CODE_MAX && rd_bytes(&r, code, cl) == 0) {
            code[cl] = '\0';
            for (size_t k = 0; k < code_count; ++k)
                if (strcasecmp((const char *)code, codes[k]) == 0) hit = k;
        } else if (r.bad) {
            result = GEO_ERR_FORMAT;
        }
        if (result == GEO_OK && hit != (size_t)-1) {
            found[hit] = 1;
            if (put_varint(out, key) != 0 || put_varint(out, len) != 0 ||
                put_varint(out, ek) != 0 || put_varint(out, cl) != 0 ||
                fwrite(code, 1, (size_t)cl, out) != (size_t)cl) {
                result = GEO_ERR_IO;
                break;
            }
            uint8_t chunk[4096];
            while (result == GEO_OK && r.left > 0) {
                size_t want = r.left < sizeof chunk ? (size_t)r.left : sizeof chunk;
                if (rd_bytes(&r, chunk, want) != 0) result = GEO_ERR_FORMAT;
                else if (fwrite(chunk, 1, want, out) != want) result = GEO_ERR_IO;
            }
        } else if (result == GEO_OK && rd_skip(&r, r.left) != 0) {
            result = GEO_ERR_FORMAT;
        }
        r.left = after;
        /* the entry body was read through r.left, so the stream sits at its end */
    }
    fclose(in);
    if (fclose(out) != 0 && result == GEO_OK) result = GEO_ERR_IO;
    if (result != GEO_OK) (void)remove(out_path);
    return result;
}
