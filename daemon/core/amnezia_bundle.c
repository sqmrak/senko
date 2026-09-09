#include "amnezia_bundle.h"

#include "b64.h"
#include "third_party/cJSON.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static const char k_scheme[] = "vpn://";

static void set_reason(char *reason, size_t cap, const char *text) {
    if (!reason || cap == 0) return;
    snprintf(reason, cap, "%s", text ? text : "invalid amnezia bundle");
}

/* the qt writers emit a byte order mark and the share sheets add newlines */
static void trim_input(const char **start, const char **end) {
    const char *s = *start;
    const char *e = *end;
    if (e - s >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF)
        s += 3;
    while (s < e && isspace((unsigned char)*s)) ++s;
    while (e > s && isspace((unsigned char)e[-1])) --e;
    *start = s;
    *end = e;
}

static int has_scheme(const char *s, const char *e) {
    size_t n = sizeof k_scheme - 1;
    return (size_t)(e - s) > n && strncmp(s, k_scheme, n) == 0;
}

/* a base64 body long enough to hold a compressed profile, nothing else */
static int looks_like_base64_body(const char *s, const char *e) {
    if (e - s < 64) return 0;
    for (const char *p = s; p < e; ++p) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '+' || c == '/' || c == '-' || c == '_' || c == '=')
            continue;
        if (isspace(c)) continue;
        return 0;
    }
    return 1;
}

/* the input is a byte range, not a c string: a pasted blob is not terminated */
static int span_contains(const char *s, const char *e, const char *needle) {
    size_t n = strlen(needle);
    if ((size_t)(e - s) < n) return 0;
    for (const char *p = s; (size_t)(e - p) >= n; ++p)
        if (memcmp(p, needle, n) == 0) return 1;
    return 0;
}

int amz_bundle_looks_like(const char *text, size_t len) {
    if (!text || len == 0) return 0;
    const char *s = text;
    const char *e = text + len;
    trim_input(&s, &e);
    if (s >= e) return 0;
    if (has_scheme(s, e)) return 1;
    /* a bare base64 body is indistinguishable from a base64 subscription feed,
       so only the scheme and the plain json shape classify without decoding */
    return *s == '{' && (span_contains(s, e, "\"containers\"") ||
                         span_contains(s, e, "\"last_config\""));
}

static uint32_t read_be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* qCompress prefixes the raw deflate stream with the uncompressed size, and the
   declared size is untrusted input, so it only ever shrinks the local bound */
static amz_status_t qt_uncompress(const unsigned char *in, size_t in_len,
                                  char **out, size_t *out_len) {
    if (in_len < 6) return AMZ_ERR_DECODE;
    uint32_t declared = read_be32(in);
    if (declared == 0 || declared > AMZ_BUNDLE_MAX_JSON) return AMZ_ERR_DECODE;
    char *buf = (char *)malloc((size_t)declared + 1);
    if (!buf) return AMZ_ERR_SPACE;

    z_stream zs;
    memset(&zs, 0, sizeof zs);
    zs.next_in = (Bytef *)(uintptr_t)(in + 4);
    zs.avail_in = (uInt)(in_len - 4);
    zs.next_out = (Bytef *)buf;
    zs.avail_out = (uInt)declared;
    if (inflateInit(&zs) != Z_OK) {
        free(buf);
        return AMZ_ERR_DECODE;
    }
    int rc = inflate(&zs, Z_FINISH);
    size_t produced = (size_t)zs.total_out;
    inflateEnd(&zs);
    if ((rc != Z_STREAM_END && rc != Z_OK && rc != Z_BUF_ERROR) || produced == 0) {
        free(buf);
        return AMZ_ERR_DECODE;
    }
    buf[produced] = '\0';
    *out = buf;
    *out_len = produced;
    return AMZ_OK;
}

/* the container that carries a wireguard profile, preferring the one the
   bundle names as default so a multi protocol share picks the same one twice */
static const cJSON *pick_protocol(const cJSON *root, const char **out_kind) {
    const cJSON *containers = cJSON_GetObjectItem((cJSON *)root, "containers");
    if (!cJSON_IsArray(containers)) return NULL;
    const cJSON *preferred = cJSON_GetObjectItem((cJSON *)root, "defaultContainer");
    const char *want = cJSON_IsString(preferred) ? preferred->valuestring : NULL;
    static const char *kinds[] = { "awg", "wireguard", NULL };

    for (int pass = 0; pass < 2; ++pass) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, containers) {
            if (!cJSON_IsObject(item)) continue;
            if (pass == 0 && want) {
                const cJSON *name = cJSON_GetObjectItem((cJSON *)item, "container");
                if (!cJSON_IsString(name) || strcmp(name->valuestring, want) != 0)
                    continue;
            } else if (pass == 0) {
                continue;
            }
            for (size_t i = 0; kinds[i]; ++i) {
                const cJSON *proto = cJSON_GetObjectItem((cJSON *)item, kinds[i]);
                if (cJSON_IsObject(proto)) {
                    if (out_kind) *out_kind = kinds[i];
                    return proto;
                }
            }
        }
    }
    return NULL;
}

/* last_config is a json document stored as a json string; a few generators
   store the object directly, and both spellings carry the same config field */
static const char *config_text_of(const cJSON *proto, cJSON **owned) {
    const cJSON *last = cJSON_GetObjectItem((cJSON *)proto, "last_config");
    if (cJSON_IsObject(last)) {
        const cJSON *cfg = cJSON_GetObjectItem((cJSON *)last, "config");
        return cJSON_IsString(cfg) ? cfg->valuestring : NULL;
    }
    if (!cJSON_IsString(last)) return NULL;
    cJSON *inner = cJSON_Parse(last->valuestring);
    if (!inner) return NULL;
    const cJSON *cfg = cJSON_GetObjectItem(inner, "config");
    if (!cJSON_IsString(cfg)) {
        cJSON_Delete(inner);
        return NULL;
    }
    *owned = inner;
    return cfg->valuestring;
}

static amz_status_t emit(const char *conf, char *out, size_t cap, size_t *out_len,
                         char *reason, size_t reason_cap) {
    size_t n = strlen(conf);
    while (n && (conf[n - 1] == '\n' || conf[n - 1] == '\r' || conf[n - 1] == ' '))
        --n;
    if (n == 0) {
        set_reason(reason, reason_cap, "amnezia bundle carries an empty config");
        return AMZ_ERR_JSON;
    }
    if (n + 2 > cap) {
        set_reason(reason, reason_cap, "amnezia config does not fit");
        return AMZ_ERR_SPACE;
    }
    memcpy(out, conf, n);
    out[n] = '\n';
    out[n + 1] = '\0';
    *out_len = n + 1;
    return AMZ_OK;
}

amz_status_t amz_bundle_extract_conf(const char *in, size_t len,
                                     char *out, size_t cap, size_t *out_len,
                                     char *reason, size_t reason_cap) {
    if (!in || !out || !out_len || cap == 0) return AMZ_ERR_ARG;
    *out_len = 0;
    /* every later path either replaces this or returns AMZ_ERR_NOT_BUNDLE, so
       the caller never reads a reason nobody wrote */
    set_reason(reason, reason_cap, "invalid amnezia bundle");
    const char *s = in;
    const char *e = in + len;
    trim_input(&s, &e);
    if (s >= e) return AMZ_ERR_NOT_BUNDLE;

    char *json = NULL;
    size_t json_len = 0;
    if (*s == '{') {
        if ((size_t)(e - s) > AMZ_BUNDLE_MAX_JSON) {
            set_reason(reason, reason_cap, "amnezia bundle is too large");
            return AMZ_ERR_SPACE;
        }
        json_len = (size_t)(e - s);
        json = (char *)malloc(json_len + 1);
        if (!json) return AMZ_ERR_SPACE;
        memcpy(json, s, json_len);
        json[json_len] = '\0';
    } else {
        if (has_scheme(s, e)) s += sizeof k_scheme - 1;
        else if (!looks_like_base64_body(s, e)) return AMZ_ERR_NOT_BUNDLE;
        size_t body_len = (size_t)(e - s);
        if (body_len > AMZ_BUNDLE_MAX_JSON) {
            set_reason(reason, reason_cap, "amnezia bundle is too large");
            return AMZ_ERR_SPACE;
        }
        size_t raw_cap = b64_decoded_maxlen(body_len);
        unsigned char *raw = (unsigned char *)malloc(raw_cap);
        if (!raw) return AMZ_ERR_SPACE;
        size_t raw_len = 0;
        if (b64_decode(s, body_len, raw, raw_cap, &raw_len) != 0 || raw_len == 0) {
            free(raw);
            set_reason(reason, reason_cap, "amnezia bundle is not base64");
            return AMZ_ERR_DECODE;
        }
        if (raw[0] == '{') {
            json_len = raw_len;
            json = (char *)malloc(json_len + 1);
            if (json) {
                memcpy(json, raw, json_len);
                json[json_len] = '\0';
            }
            free(raw);
            if (!json) return AMZ_ERR_SPACE;
        } else {
            amz_status_t r = qt_uncompress(raw, raw_len, &json, &json_len);
            free(raw);
            if (r != AMZ_OK) {
                set_reason(reason, reason_cap, "amnezia bundle does not decompress");
                return r;
            }
        }
    }

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    free(json);
    if (!root) {
        set_reason(reason, reason_cap, "amnezia bundle is not json");
        return AMZ_ERR_JSON;
    }

    const char *kind = NULL;
    const cJSON *proto = pick_protocol(root, &kind);
    if (!proto) {
        cJSON_Delete(root);
        set_reason(reason, reason_cap,
                   "amnezia bundle has no amneziawg or wireguard container");
        return AMZ_ERR_UNSUPPORTED;
    }

    cJSON *owned = NULL;
    const char *conf = config_text_of(proto, &owned);
    if (!conf) {
        if (owned) cJSON_Delete(owned);
        cJSON_Delete(root);
        set_reason(reason, reason_cap, "amnezia container has no config text");
        return AMZ_ERR_JSON;
    }

    amz_status_t r = emit(conf, out, cap, out_len, reason, reason_cap);
    if (owned) cJSON_Delete(owned);
    cJSON_Delete(root);
    if (r == AMZ_OK) set_reason(reason, reason_cap, "ok");
    return r;
}
