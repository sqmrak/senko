#ifndef SENKO_GEO_UPDATE_H
#define SENKO_GEO_UPDATE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "geo.h"

#ifdef __cplusplus
extern "C" {
#endif

/* keeps the per tag geo files of the active routing profile in step with it.
   the plan says whether a download is due, the run downloads each dat once into
   a temporary file, extracts the tags and deletes the dat */

#define GEO_UPDATE_TAGS 128
#define GEO_UPDATE_MAX_BYTES (64u * 1024u * 1024u)
/* happ keeps its geo files for a week before it asks the host again */
#define GEO_UPDATE_STALE_SECONDS (7u * 24u * 3600u)
#define GEO_UPDATE_DIR_MAX 256

typedef struct {
    char dir[GEO_UPDATE_DIR_MAX];
    char site_url[512];
    char ip_url[512];
    char site_tags[GEO_UPDATE_TAGS][GEO_TAG_MAX];
    size_t site_n;
    char ip_tags[GEO_UPDATE_TAGS][GEO_TAG_MAX];
    size_t ip_n;
} geo_update_job_t;

typedef struct {
    int ok;             /* every dat was fetched and read */
    size_t written;     /* tag files written */
    size_t missing;     /* tags the dat does not hold; they match nothing */
    size_t regexp_skipped;
    char error[200];
} geo_update_result_t;

/* downloads url into out, at most max_bytes, and returns 0. on failure it
   returns -1 and puts a short reason for the user in error */
typedef int (*geo_fetch_fn)(void *ctx, const char *url, FILE *out, size_t max_bytes,
                            size_t *written, char *error, size_t error_cap);

/* fills job from a profile json and returns 1 when a download is due: a tag has
   no file yet, force is set, or the files are older than a week. the profile
   json comes from the active subscription */
int geo_update_plan(const char *dir, const char *profile_json, size_t len,
                    uint64_t now, int force, geo_update_job_t *job);

void geo_update_run(const geo_update_job_t *job, geo_fetch_fn fetch, void *ctx,
                    uint64_t now, geo_update_result_t *result);

/* seconds since the epoch of the last complete update, 0 when there was none */
uint64_t geo_update_stamp(const char *dir);

/* how many tags of the profile have no file yet */
size_t geo_update_missing(const char *dir, const char *profile_json, size_t len);

#ifdef __cplusplus
}
#endif

#endif
