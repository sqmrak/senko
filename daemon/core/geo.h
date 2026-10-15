#ifndef SENKO_GEO_H
#define SENKO_GEO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the domain and address sets that geosite: and geoip: rules name.

   a v2ray geosite.dat or geoip.dat is far too big to hold on an old device, so
   the daemon streams one through geo_dat_extract once and keeps only the tags
   the active routing profile names, one small file per tag. the rules then load
   those files, never the dat */

#define GEO_TAG_MAX 64
#define GEO_SITE_MAX_ENTRIES 400000u
#define GEO_IP_MAX_RANGES 200000u
#define GEO_KEYWORD_MAX 256u
#define GEO_KEYWORD_LEN_MAX 64u

typedef enum {
    GEO_OK = 0,
    GEO_ERR_ARG = -1,
    GEO_ERR_IO = -2,
    GEO_ERR_FORMAT = -3,
    GEO_ERR_MEMORY = -4,
    GEO_ERR_TOOBIG = -5
} geo_status_t;

typedef enum {
    GEO_KIND_SITE = 0,
    GEO_KIND_IP
} geo_kind_t;

typedef enum {
    GEO_SITE_SUFFIX = 0,
    GEO_SITE_FULL,
    GEO_SITE_KEYWORD
} geo_site_kind_t;

typedef struct geo_site geo_site_t;
typedef struct geo_ip geo_ip_t;

/* a domain set. suffix entries match the name and every subdomain, full
   entries match the name only, keyword entries match anywhere inside it */
geo_site_t *geo_site_new(void);
void geo_site_free(geo_site_t *set);
geo_status_t geo_site_add(geo_site_t *set, geo_site_kind_t kind,
                          const char *value, size_t len);
/* sorts and drops duplicates; a set must be sealed before it is matched or saved */
geo_status_t geo_site_seal(geo_site_t *set);
/* domain is lower case without a trailing dot */
int geo_site_match(const geo_site_t *set, const char *domain);
size_t geo_site_count(const geo_site_t *set);

/* an address set of merged v4 and v6 ranges. a reversed set matches what its
   ranges do not cover */
geo_ip_t *geo_ip_new(void);
void geo_ip_free(geo_ip_t *set);
geo_status_t geo_ip_add(geo_ip_t *set, const uint8_t *address, uint8_t address_len,
                        uint8_t prefix);
void geo_ip_set_reverse(geo_ip_t *set, int reverse);
geo_status_t geo_ip_seal(geo_ip_t *set);
int geo_ip_match(const geo_ip_t *set, const uint8_t *address, uint8_t address_len);
size_t geo_ip_count(const geo_ip_t *set);

/* the private ranges xray names geoip:private */
geo_ip_t *geo_ip_private(void);

geo_status_t geo_site_save(const geo_site_t *set, const char *path);
geo_status_t geo_site_load(const char *path, geo_site_t **out);
geo_status_t geo_ip_save(const geo_ip_t *set, const char *path);
geo_status_t geo_ip_load(const char *path, geo_ip_t **out);

/* where the file for a tag lives. a tag is "name" or "name@attribute", lower
   case, from [a-z0-9._@!-] and starting with a letter or digit; anything else is
   refused so a profile cannot name a path. returns 0, or -1 for a bad tag or a
   path that does not fit */
int geo_tag_valid(const char *tag);
int geo_tag_path(const char *dir, geo_kind_t kind, const char *tag,
                 char *out, size_t cap);

typedef struct {
    size_t entries_seen;   /* entries in the dat */
    size_t tags_found;
    size_t regexp_skipped; /* geosite regexp rules, which senko cannot match */
} geo_extract_stats_t;

/* streams a dat once and writes one file per requested tag into dir. found[i]
   says whether specs[i] was in the dat; a tag the dat lacks writes nothing. the
   dat is read with a fixed buffer and an entry of a tag nobody asked for is
   skipped by seeking, so memory stays bound by the requested sets */
geo_status_t geo_dat_extract(const char *dat_path, geo_kind_t kind, const char *dir,
                             const char *const *specs, size_t spec_count,
                             uint8_t *found, geo_extract_stats_t *stats);

/* writes the entries of the named codes (a code is a tag without its
   @attribute) from a dat into a smaller dat, byte for byte. senko-core reads
   its geosite: and geoip: rules from such a file through xray, which only needs
   the codes the rules name. found[i] says whether codes[i] was in the dat */
geo_status_t geo_dat_slice(const char *dat_path, const char *out_path,
                           const char *const *codes, size_t code_count, uint8_t *found);

#ifdef __cplusplus
}
#endif

#endif
