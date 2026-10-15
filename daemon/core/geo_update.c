#define _POSIX_C_SOURCE 200809L

#include "geo_update.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "routing_profile.h"

#define STAMP_NAME "stamp"
#define DAT_TMP_NAME "download.tmp"

static int path_join(char *out, size_t cap, const char *dir, const char *name) {
    int n = snprintf(out, cap, "%s/%s", dir, name);
    return n > 0 && (size_t)n < cap ? 0 : -1;
}

uint64_t geo_update_stamp(const char *dir) {
    char path[GEO_UPDATE_DIR_MAX + 16];
    char text[32];
    if (!dir || path_join(path, sizeof path, dir, STAMP_NAME) != 0) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = '\0';
    char *stop = NULL;
    unsigned long long v = strtoull(text, &stop, 10);
    return stop != text ? (uint64_t)v : 0;
}

static int write_stamp(const char *dir, uint64_t now) {
    char path[GEO_UPDATE_DIR_MAX + 16];
    if (path_join(path, sizeof path, dir, STAMP_NAME) != 0) return -1;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int ok = fprintf(f, "%llu\n", (unsigned long long)now) > 0;
    ok = fclose(f) == 0 && ok;
    return ok ? 0 : -1;
}

static int tag_present(const char *dir, geo_kind_t kind, const char *tag) {
    char path[512];
    if (geo_tag_path(dir, kind, tag, path, sizeof path) != 0) return 0;
    return access(path, R_OK) == 0;
}

size_t geo_update_missing(const char *dir, const char *profile_json, size_t len) {
    static char tags[GEO_UPDATE_TAGS][GEO_TAG_MAX];
    size_t missing = 0;
    for (int k = 0; k < 2; ++k) {
        geo_kind_t kind = k ? GEO_KIND_IP : GEO_KIND_SITE;
        size_t n = routing_geo_tags(profile_json, len, kind, tags, GEO_UPDATE_TAGS);
        if (n > GEO_UPDATE_TAGS) n = GEO_UPDATE_TAGS;
        for (size_t i = 0; i < n; ++i)
            if (!tag_present(dir, kind, tags[i])) ++missing;
    }
    return missing;
}

int geo_update_plan(const char *dir, const char *profile_json, size_t len,
                    uint64_t now, int force, geo_update_job_t *job) {
    routing_info_t info;
    if (!dir || !profile_json || !job || strlen(dir) >= sizeof job->dir) return 0;
    memset(job, 0, sizeof *job);
    if (routing_inspect(profile_json, len, &info) != ROUTING_OK) return 0;
    snprintf(job->dir, sizeof job->dir, "%s", dir);
    snprintf(job->site_url, sizeof job->site_url, "%s", info.geosite_url);
    snprintf(job->ip_url, sizeof job->ip_url, "%s", info.geoip_url);
    job->site_n = routing_geo_tags(profile_json, len, GEO_KIND_SITE, job->site_tags,
                                   GEO_UPDATE_TAGS);
    job->ip_n = routing_geo_tags(profile_json, len, GEO_KIND_IP, job->ip_tags,
                                 GEO_UPDATE_TAGS);
    if (job->site_n > GEO_UPDATE_TAGS) job->site_n = GEO_UPDATE_TAGS;
    if (job->ip_n > GEO_UPDATE_TAGS) job->ip_n = GEO_UPDATE_TAGS;
    if (job->site_n + job->ip_n == 0) return 0;
    if (force || geo_update_missing(dir, profile_json, len) > 0) return 1;
    uint64_t stamp = geo_update_stamp(dir);
    return stamp == 0 || now < stamp || now - stamp >= GEO_UPDATE_STALE_SECONDS;
}

static void set_error(geo_update_result_t *r, const char *fmt, const char *a) {
    snprintf(r->error, sizeof r->error, fmt, a ? a : "");
}

/* a tag the dat lacks still gets a file, an empty one, so the profile does not
   send the daemon back to the network on every refresh to look for it again */
static geo_status_t write_empty(const char *dir, geo_kind_t kind, const char *tag) {
    char path[512];
    if (geo_tag_path(dir, kind, tag, path, sizeof path) != 0) return GEO_ERR_ARG;
    if (kind == GEO_KIND_SITE) {
        geo_site_t *set = geo_site_new();
        if (!set) return GEO_ERR_MEMORY;
        geo_site_seal(set);
        geo_status_t s = geo_site_save(set, path);
        geo_site_free(set);
        return s;
    }
    geo_ip_t *set = geo_ip_new();
    if (!set) return GEO_ERR_MEMORY;
    geo_ip_seal(set);
    geo_status_t s = geo_ip_save(set, path);
    geo_ip_free(set);
    return s;
}

/* senko-core reads geosite: and geoip: through xray, which wants a dat in its
   asset directory. a copy of only the entries the profile names keeps that file
   small, and an xray rule never meets a code the file lacks, because only codes
   found here get rendered */
static geo_status_t write_slice(const geo_update_job_t *job, geo_kind_t kind,
                                const char *dat, const char (*tags)[GEO_TAG_MAX], size_t n) {
    const char *codes[GEO_UPDATE_TAGS];
    char base[GEO_UPDATE_TAGS][GEO_TAG_MAX];
    uint8_t found[GEO_UPDATE_TAGS];
    size_t count = 0;
    char path[GEO_UPDATE_DIR_MAX + 32], part[GEO_UPDATE_DIR_MAX + 40];
    if (path_join(path, sizeof path, job->dir,
                  kind == GEO_KIND_SITE ? "geosite.dat" : "geoip.dat") != 0 ||
        snprintf(part, sizeof part, "%s.new", path) >= (int)sizeof part)
        return GEO_ERR_ARG;
    for (size_t i = 0; i < n; ++i) {
        const char *at = strchr(tags[i], '@');
        size_t len = at ? (size_t)(at - tags[i]) : strlen(tags[i]);
        int seen = 0;
        for (size_t k = 0; k < count && !seen; ++k)
            seen = strlen(base[k]) == len && strncmp(base[k], tags[i], len) == 0;
        if (seen || len == 0 || len >= GEO_TAG_MAX) continue;
        memcpy(base[count], tags[i], len);
        base[count][len] = '\0';
        codes[count] = base[count];
        ++count;
    }
    geo_status_t s = geo_dat_slice(dat, part, codes, count, found);
    if (s == GEO_OK && rename(part, path) != 0) {
        (void)unlink(part);
        s = GEO_ERR_IO;
    }
    return s;
}

static int run_kind(const geo_update_job_t *job, geo_kind_t kind, geo_fetch_fn fetch,
                    void *ctx, geo_update_result_t *res) {
    const char *url = kind == GEO_KIND_SITE ? job->site_url : job->ip_url;
    size_t n = kind == GEO_KIND_SITE ? job->site_n : job->ip_n;
    const char (*tags)[GEO_TAG_MAX] = kind == GEO_KIND_SITE ? job->site_tags : job->ip_tags;
    const char *specs[GEO_UPDATE_TAGS];
    uint8_t found[GEO_UPDATE_TAGS];
    char tmp[GEO_UPDATE_DIR_MAX + 32];
    char why[160];
    size_t got = 0;
    geo_extract_stats_t stats;
    if (n == 0) return 0;
    if (path_join(tmp, sizeof tmp, job->dir, DAT_TMP_NAME) != 0) {
        set_error(res, "the geo directory path is too long%s", "");
        return -1;
    }
    FILE *out = fopen(tmp, "wb");
    if (!out) {
        set_error(res, "cannot write the geo download: %s", strerror(errno));
        return -1;
    }
    why[0] = '\0';
    int rc = fetch(ctx, url, out, GEO_UPDATE_MAX_BYTES, &got, why, sizeof why);
    int closed = fclose(out) == 0;
    if (rc != 0 || !closed) {
        (void)unlink(tmp);
        set_error(res, "%s", why[0] ? why : closed ? "the geo download failed"
                                                   : "the geo download could not be saved");
        return -1;
    }
    for (size_t i = 0; i < n; ++i) specs[i] = tags[i];
    geo_status_t s = geo_dat_extract(tmp, kind, job->dir, specs, n, found, &stats);
    if (s == GEO_OK) s = write_slice(job, kind, tmp, tags, n);
    (void)unlink(tmp);
    if (s != GEO_OK) {
        set_error(res, s == GEO_ERR_FORMAT ? "the downloaded %s is not a geo dat file"
                                           : "the downloaded %s could not be read",
                  kind == GEO_KIND_SITE ? "geosite file" : "geoip file");
        return -1;
    }
    res->regexp_skipped += stats.regexp_skipped;
    for (size_t i = 0; i < n; ++i) {
        if (found[i]) {
            ++res->written;
            continue;
        }
        ++res->missing;
        if (write_empty(job->dir, kind, tags[i]) != GEO_OK) {
            set_error(res, "cannot write the geo file of %s", tags[i]);
            return -1;
        }
        if (!res->error[0])
            snprintf(res->error, sizeof res->error, "%s tag %s is not in the dat file",
                     kind == GEO_KIND_SITE ? "geosite" : "geoip", tags[i]);
    }
    return 0;
}

/* a tag the profile dropped, or a profile that went away, would leave its file
   behind for good, and a geo directory should not outgrow the profile it serves */
static void prune(const geo_update_job_t *job) {
    DIR *d = opendir(job->dir);
    struct dirent *e;
    char keep[2 * GEO_UPDATE_TAGS][128];
    size_t kept = 0;
    char path[512];
    if (!d) return;
    /* the path of a tag under an empty directory is "/name", the name is wanted */
    for (size_t i = 0; i < job->site_n && kept < 2 * GEO_UPDATE_TAGS; ++i)
        if (geo_tag_path("", GEO_KIND_SITE, job->site_tags[i], keep[kept], sizeof keep[0]) == 0)
            ++kept;
    for (size_t i = 0; i < job->ip_n && kept < 2 * GEO_UPDATE_TAGS; ++i)
        if (geo_tag_path("", GEO_KIND_IP, job->ip_tags[i], keep[kept], sizeof keep[0]) == 0)
            ++kept;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        int ours = len > 4 && strcmp(e->d_name + len - 4, ".bin") == 0 &&
                   (strncmp(e->d_name, "site-", 5) == 0 || strncmp(e->d_name, "ip-", 3) == 0);
        int wanted = 0;
        if (!ours) continue;
        for (size_t i = 0; i < kept; ++i)
            if (strcmp(keep[i] + 1, e->d_name) == 0) wanted = 1;
        if (wanted || path_join(path, sizeof path, job->dir, e->d_name) != 0) continue;
        (void)unlink(path);
    }
    closedir(d);
}

void geo_update_run(const geo_update_job_t *job, geo_fetch_fn fetch, void *ctx,
                    uint64_t now, geo_update_result_t *result) {
    memset(result, 0, sizeof *result);
    if (!job || !fetch) {
        set_error(result, "no geo download was set up%s", "");
        return;
    }
    if (mkdir(job->dir, 0700) != 0 && errno != EEXIST) {
        set_error(result, "cannot create the geo directory: %s", strerror(errno));
        return;
    }
    /* a tag the dat lacks is reported but is not a failed update: the dat was
       read and the files that exist were written */
    if (run_kind(job, GEO_KIND_SITE, fetch, ctx, result) != 0) return;
    if (run_kind(job, GEO_KIND_IP, fetch, ctx, result) != 0) return;
    prune(job);
    if (write_stamp(job->dir, now) != 0) {
        set_error(result, "cannot record the geo update time: %s", strerror(errno));
        return;
    }
    result->ok = 1;
}
