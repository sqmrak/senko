#include "store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void store_init(store_t *st) {
    if (!st) return;
    memset(st, 0, sizeof *st);
    st->selected = -1;
    for (size_t i = 0; i < STORE_MAX_SERVERS; ++i) st->group[i] = STORE_GROUP_MANUAL;
}

static int same_server(const vl_server_t *a, const vl_server_t *b) {
    return a->port == b->port
        && a->proto == b->proto
        && a->net == b->net
        && a->security == b->security
        && strcmp(a->user, b->user) == 0
        && strcmp(a->pass, b->pass) == 0
        && strcmp(a->uuid, b->uuid) == 0
        && strcmp(a->host, b->host) == 0;
}

static int section_seen(const int *order, size_t n, int id) {
    for (size_t i = 0; i < n; ++i)
        if (order[i] == id) return 1;
    return 0;
}

store_status_t store_add_manual(store_t *st, const char *link, size_t *out_index) {
    if (!st || !link) return STORE_ERR_ARG;
    if (st->n >= STORE_MAX_SERVERS) return STORE_ERR_FULL;

    vl_server_t tmp;
    if (cfg_parse_link(link, &tmp) != CFG_OK) return STORE_ERR_PARSE;
    if (!cfg_validate_server(&tmp, NULL, 0)) return STORE_ERR_UNSUPPORTED;

    for (size_t i = 0; i < st->n; ++i) {
        if (same_server(&st->servers[i], &tmp)) {
            if (out_index) *out_index = i;
            return STORE_ERR_EXISTS;
        }
    }

    st->servers[st->n] = tmp;
    st->group[st->n] = STORE_GROUP_MANUAL;
    if (out_index) *out_index = st->n;
    st->n++;
    return STORE_OK;
}

store_status_t store_add_sub(store_t *st, const char *name, const char *url,
                             size_t *out_sub) {
    if (!st || !name || !url) return STORE_ERR_ARG;
    size_t nl = strlen(name);
    size_t ul = strlen(url);
    if (nl >= 64 || ul >= 512) return STORE_ERR_TOO_LONG;

    for (size_t i = 0; i < STORE_MAX_SUBS; ++i) {
        if (!st->subs[i].used) continue;
        if (strcmp(st->subs[i].url, url) == 0) {
            if (out_sub) *out_sub = i;
            return STORE_OK;
        }
    }

    for (size_t i = 0; i < STORE_MAX_SUBS; ++i) {
        if (st->subs[i].used) continue;
        store_sub_t *s = &st->subs[i];
        memcpy(s->name, name, nl); s->name[nl] = '\0';
        memcpy(s->url,  url,  ul); s->url[ul]  = '\0';
        s->used = 1;
        if (st->section_n == 0) {
            st->section_order[0] = STORE_GROUP_MANUAL;
            st->section_n = 1;
        }
        if (st->section_n < STORE_MAX_SUBS + 1) {
            st->section_order[st->section_n++] = (int)i;
        }
        if (out_sub) *out_sub = i;
        return STORE_OK;
    }
    return STORE_ERR_FULL;
}

void store_normalize(store_t *st) {
    if (!st) return;

    int used[STORE_MAX_SUBS];
    int n_used = 0;
    for (size_t i = 0; i < STORE_MAX_SUBS; ++i) {
        if (st->subs[i].used) used[n_used++] = (int)i;
    }

    int old_order[STORE_MAX_SUBS + 1];
    size_t old_n = st->section_n;
    if (old_n > STORE_MAX_SUBS + 1) old_n = STORE_MAX_SUBS + 1;
    memcpy(old_order, st->section_order, old_n * sizeof old_order[0]);
    st->section_n = 0;
    if (old_n == 0) {
        st->section_order[st->section_n++] = STORE_GROUP_MANUAL;
        for (int i = 0; i < n_used; ++i)
            st->section_order[st->section_n++] = used[i];
    } else {
        for (size_t i = 0; i < old_n; ++i) {
            int id = old_order[i];
            if (id != STORE_GROUP_MANUAL &&
                (id < 0 || id >= STORE_MAX_SUBS || !st->subs[id].used))
                continue;
            if (!section_seen(old_order, i, id) && st->section_n < STORE_MAX_SUBS + 1)
                st->section_order[st->section_n++] = id;
        }
        for (int i = 0; i < n_used; ++i) {
            if (!section_seen(st->section_order, st->section_n, used[i]) &&
                st->section_n < STORE_MAX_SUBS + 1)
                st->section_order[st->section_n++] = used[i];
        }
        if (!section_seen(st->section_order, st->section_n, STORE_GROUP_MANUAL) &&
            st->section_n < STORE_MAX_SUBS + 1)
            st->section_order[st->section_n++] = STORE_GROUP_MANUAL;
    }

    int orphan_mark[STORE_MAX_SUBS];
    memset(orphan_mark, 0, sizeof orphan_mark);
    int n_orphan = 0;
    for (size_t i = 0; i < st->n; ++i) {
        int g = st->group[i];
        if (g < 0 || g >= STORE_MAX_SUBS) continue;
        if (st->subs[g].used) continue;
        if (!orphan_mark[g]) {
            orphan_mark[g] = 1;
            n_orphan++;
        }
    }
    if (n_orphan == 0) return;

    if (n_used == 1) {
        int target = used[0];
        for (size_t i = 0; i < st->n; ++i) {
            int g = st->group[i];
            if (g >= 0 && g < STORE_MAX_SUBS && !st->subs[g].used)
                st->group[i] = target;
        }
        return;
    }

    for (size_t i = 0; i < st->n; ++i) {
        int g = st->group[i];
        if (g >= 0 && g < STORE_MAX_SUBS && !st->subs[g].used)
            st->group[i] = STORE_GROUP_MANUAL;
    }
}

size_t store_section_count(const store_t *st) {
    return st ? st->section_n : 0;
}

int store_section_at(const store_t *st, size_t pos) {
    if (!st || pos >= st->section_n) return STORE_GROUP_MANUAL - 1;
    return st->section_order[pos];
}

store_status_t store_move_section(store_t *st, int section_id, size_t to_pos) {
    if (!st) return STORE_ERR_ARG;
    store_normalize(st);
    if (section_id != STORE_GROUP_MANUAL &&
        (section_id < 0 || section_id >= STORE_MAX_SUBS || !st->subs[section_id].used))
        return STORE_ERR_RANGE;

    size_t from = st->section_n;
    for (size_t i = 0; i < st->section_n; ++i) {
        if (st->section_order[i] == section_id) { from = i; break; }
    }
    if (from == st->section_n) return STORE_ERR_RANGE;
    if (to_pos >= st->section_n) to_pos = st->section_n - 1;
    if (from == to_pos) return STORE_OK;

    int id = st->section_order[from];
    if (from < to_pos) {
        memmove(&st->section_order[from], &st->section_order[from + 1],
                (to_pos - from) * sizeof st->section_order[0]);
    } else {
        memmove(&st->section_order[to_pos + 1], &st->section_order[to_pos],
                (from - to_pos) * sizeof st->section_order[0]);
    }
    st->section_order[to_pos] = id;
    return STORE_OK;
}

static void drop_group(store_t *st, int grp) {
    size_t w = 0;
    for (size_t r = 0; r < st->n; ++r) {
        if (st->group[r] == grp) continue; /* drop this row */
        if (w != r) {
            st->servers[w] = st->servers[r];
            st->group[w]   = st->group[r];
        }
        w++;
    }
    st->n = w;
}

store_status_t store_refresh_sub(store_t *st, size_t sub_index,
                                 const char *blob, size_t blob_len,
                                 size_t *out_added) {
    if (!st || !blob) return STORE_ERR_ARG;
    if (sub_index >= STORE_MAX_SUBS || !st->subs[sub_index].used)
        return STORE_ERR_RANGE;
    if (out_added) *out_added = 0;

    vl_server_t prev_sel;
    int had_sel = (st->selected >= 0 && (size_t)st->selected < st->n);
    if (had_sel) prev_sel = st->servers[st->selected];

    size_t old_group_count = 0;
    for (size_t i = 0; i < st->n; ++i)
        if (st->group[i] == (int)sub_index) old_group_count++;
    size_t room = STORE_MAX_SERVERS - st->n + old_group_count;
    if (room == 0) return STORE_ERR_FULL;
    vl_server_t *fresh = (vl_server_t *)calloc(room, sizeof *fresh);
    if (!fresh) return STORE_ERR_FULL;
    size_t added = 0;
    cfg_parse_subscription(blob, blob_len, fresh, room, &added);
    if (added == 0) {
        free(fresh);
        return STORE_ERR_PARSE;
    }
    drop_group(st, (int)sub_index);
    memcpy(&st->servers[st->n], fresh, added * sizeof *fresh);
    for (size_t i = 0; i < added; ++i) st->group[st->n + i] = (int)sub_index;
    st->n += added;
    free(fresh);
    if (out_added) *out_added = added;

    if (had_sel) {
        st->selected = -1;
        for (size_t i = 0; i < st->n; ++i) {
            if (same_server(&st->servers[i], &prev_sel)) { st->selected = (int)i; break; }
        }
    }
    return STORE_OK;
}

store_status_t store_remove(store_t *st, size_t index) {
    if (!st) return STORE_ERR_ARG;
    if (index >= st->n) return STORE_ERR_RANGE;

    for (size_t r = index + 1; r < st->n; ++r) {
        st->servers[r - 1] = st->servers[r];
        st->group[r - 1]   = st->group[r];
    }
    st->n--;

    if (st->selected == (int)index)      st->selected = -1;
    else if (st->selected > (int)index)  st->selected--;
    return STORE_OK;
}

store_status_t store_move_manual(store_t *st, size_t index, size_t to_pos) {
    if (!st) return STORE_ERR_ARG;
    if (index >= st->n || st->group[index] != STORE_GROUP_MANUAL)
        return STORE_ERR_RANGE;

    size_t manual_n = 0;
    for (size_t i = 0; i < st->n; ++i)
        if (st->group[i] == STORE_GROUP_MANUAL && i != index) manual_n++;
    if (to_pos > manual_n) to_pos = manual_n;

    vl_server_t moved = st->servers[index];
    int moved_group = st->group[index];
    vl_server_t selected;
    int had_selected = st->selected >= 0 && (size_t)st->selected < st->n;
    if (had_selected) selected = st->servers[st->selected];
    vl_server_t next[STORE_MAX_SERVERS];
    int next_group[STORE_MAX_SERVERS];
    size_t out = 0, seen = 0;
    int inserted = 0;
    for (size_t i = 0; i < st->n; ++i) {
        if (!inserted && seen == to_pos) {
            next[out] = moved;
            next_group[out++] = moved_group;
            inserted = 1;
        }
        if (i == index) continue;
        next[out] = st->servers[i];
        next_group[out++] = st->group[i];
        if (st->group[i] == STORE_GROUP_MANUAL) seen++;
    }
    if (!inserted) {
        next[out] = moved;
        next_group[out++] = moved_group;
    }
    memcpy(st->servers, next, out * sizeof next[0]);
    memcpy(st->group, next_group, out * sizeof next_group[0]);
    st->n = out;
    st->selected = -1;
    if (had_selected) {
        for (size_t i = 0; i < st->n; ++i) {
            if (same_server(&st->servers[i], &selected)) {
                st->selected = (int)i;
                break;
            }
        }
    }
    return STORE_OK;
}

store_status_t store_remove_sub(store_t *st, size_t sub_index) {
    if (!st) return STORE_ERR_ARG;
    if (sub_index >= STORE_MAX_SUBS || !st->subs[sub_index].used)
        return STORE_ERR_RANGE;

    vl_server_t prev_sel;
    int had_sel = (st->selected >= 0 && (size_t)st->selected < st->n);
    if (had_sel) prev_sel = st->servers[st->selected];

    drop_group(st, (int)sub_index);
    memset(&st->subs[sub_index], 0, sizeof st->subs[sub_index]);
    for (size_t i = 0; i < st->section_n; ++i) {
        if (st->section_order[i] != (int)sub_index) continue;
        memmove(&st->section_order[i], &st->section_order[i + 1],
                (st->section_n - i - 1) * sizeof st->section_order[0]);
        st->section_n--;
        break;
    }

    st->selected = -1;
    if (had_sel) {
        for (size_t i = 0; i < st->n; ++i) {
            if (same_server(&st->servers[i], &prev_sel)) {
                st->selected = (int)i;
                break;
            }
        }
    }
    return STORE_OK;
}

void store_set_sub_expire(store_t *st, size_t sub_index, uint64_t expire) {
    if (!st || sub_index >= STORE_MAX_SUBS || !st->subs[sub_index].used) return;
    st->subs[sub_index].expire = expire;
}

store_status_t store_select(store_t *st, int index) {
    if (!st) return STORE_ERR_ARG;
    if (index == -1) { st->selected = -1; return STORE_OK; }
    if (index < 0 || (size_t)index >= st->n) return STORE_ERR_RANGE;
    st->selected = index;
    return STORE_OK;
}

const vl_server_t *store_selected(const store_t *st) {
    if (!st || st->selected < 0 || (size_t)st->selected >= st->n) return NULL;
    return &st->servers[st->selected];
}

static const char *security_name(vl_sec_t s) {
    switch (s) {
        case VL_SEC_REALITY: return "reality";
        case VL_SEC_TLS:     return "tls";
        case VL_SEC_NONE:    return "none";
        default:             return "none";
    }
}

static const char *net_name(vl_net_t n) {
    switch (n) {
        case VL_NET_WS:    return "ws";
        case VL_NET_GRPC:  return "grpc";
        case VL_NET_HTTP:  return "http";
        case VL_NET_XHTTP: return "xhttp";
        case VL_NET_TCP:   return "tcp";
        default:          return "tcp";
    }
}

static int pct_encode(const char *src, char *dst, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; ++p) {
        unsigned char c = *p;
        int special = (c <= 0x20 || c == '%' || c == '#' || c == '&' ||
                       c == '?' || c == '=' || c == '+');
        if (special) {
            if (o + 3 >= cap) return -1;
            dst[o++] = '%';
            dst[o++] = hex[c >> 4];
            dst[o++] = hex[c & 0xf];
        } else {
            if (o + 1 >= cap) return -1;
            dst[o++] = (char)c;
        }
    }
    if (o >= cap) return -1;
    dst[o] = '\0';
    return (int)o;
}

static int build_link(const vl_server_t *s, char *buf, size_t cap) {
    if (s->proto == VL_PROTO_SOCKS5 || s->proto == VL_PROTO_HTTP || s->proto == VL_PROTO_HTTPS) {
        const char *scheme = (s->proto == VL_PROTO_SOCKS5) ? "socks5" :
                             (s->proto == VL_PROTO_HTTPS) ? "https" : "http";
        int n;
        if (s->user[0]) {
            if (s->pass[0]) {
                n = snprintf(buf, cap, "%s://%s:%s@%s:%u", scheme, s->user, s->pass, s->host, s->port);
            } else {
                n = snprintf(buf, cap, "%s://%s@%s:%u", scheme, s->user, s->host, s->port);
            }
        } else {
            n = snprintf(buf, cap, "%s://%s:%u", scheme, s->host, s->port);
        }
        if (n < 0 || (size_t)n >= cap) return -1;
        size_t off = (size_t)n;
        if (s->remark[0]) {
            char enc[256];
            if (pct_encode(s->remark, enc, sizeof enc) < 0) return -1;
            int m = snprintf(buf + off, cap - off, "#%s", enc);
            if (m < 0 || (size_t)m >= cap - off) return -1;
            off += (size_t)m;
        }
        return (int)off;
    }

    int n = snprintf(buf, cap, "vless://%s@%s:%u?security=%s&type=%s",
                     s->uuid, s->host, s->port,
                     security_name(s->security), net_name(s->net));
    if (n < 0 || (size_t)n >= cap) return -1;
    size_t off = (size_t)n;

    struct { const char *key; const char *val; } opt[] = {
        { "sni",  s->sni  },
        { "host", s->ws_host },
        { "flow", s->flow },
        { "fp",   s->fp   },
        { "pbk",  s->pbk  },
        { "sid",  s->sid  },
        { "path", s->path },
        { "mode", s->mode },
    };
    for (size_t i = 0; i < sizeof opt / sizeof opt[0]; ++i) {
        if (!opt[i].val[0]) continue;
        char enc[1024];
        if (pct_encode(opt[i].val, enc, sizeof enc) < 0) return -1;
        int m = snprintf(buf + off, cap - off, "&%s=%s", opt[i].key, enc);
        if (m < 0 || (size_t)m >= cap - off) return -1;
        off += (size_t)m;
    }

    if (s->remark[0]) {
        char enc[256];
        if (pct_encode(s->remark, enc, sizeof enc) < 0) return -1;
        int m = snprintf(buf + off, cap - off, "#%s", enc);
        if (m < 0 || (size_t)m >= cap - off) return -1;
        off += (size_t)m;
    }
    return (int)off;
}

int store_link_at(const store_t *st, size_t index, char *buf, size_t cap) {
    if (!st || !buf || index >= st->n) return -1;
    return build_link(&st->servers[index], buf, cap) >= 0 ? 0 : -1;
}

store_status_t store_serialize(const store_t *st, char *buf, size_t cap, size_t *out_len) {
    if (!st || !buf) return STORE_ERR_ARG;
    size_t off = 0;

    int n = snprintf(buf + off, cap - off, "V1\n");
    if (n < 0 || (size_t)n >= cap - off) return STORE_ERR_FULL;
    off += (size_t)n;

    for (size_t i = 0; i < STORE_MAX_SUBS; ++i) {
        if (!st->subs[i].used) continue;
        n = snprintf(buf + off, cap - off, "SUB %zu %s %s\n",
                     i, st->subs[i].url, st->subs[i].name);
        if (n < 0 || (size_t)n >= cap - off) return STORE_ERR_FULL;
        off += (size_t)n;
        n = snprintf(buf + off, cap - off, "SUBMETA %zu %llu\n", i,
                     (unsigned long long)st->subs[i].expire);
        if (n < 0 || (size_t)n >= cap - off) return STORE_ERR_FULL;
        off += (size_t)n;
    }

    n = snprintf(buf + off, cap - off, "ORDER");
    if (n < 0 || (size_t)n >= cap - off) return STORE_ERR_FULL;
    off += (size_t)n;
    for (size_t i = 0; i < st->section_n; ++i) {
        n = snprintf(buf + off, cap - off, " %d", st->section_order[i]);
        if (n < 0 || (size_t)n >= cap - off) return STORE_ERR_FULL;
        off += (size_t)n;
    }
    n = snprintf(buf + off, cap - off, "\n");
    if (n < 0 || (size_t)n >= cap - off) return STORE_ERR_FULL;
    off += (size_t)n;

    for (size_t i = 0; i < st->n; ++i) {
        char link[2048];
        if (build_link(&st->servers[i], link, sizeof link) < 0) return STORE_ERR_FULL;
        n = snprintf(buf + off, cap - off, "SRV %d %s\n", st->group[i], link);
        if (n < 0 || (size_t)n >= cap - off) return STORE_ERR_FULL;
        off += (size_t)n;
    }

    n = snprintf(buf + off, cap - off, "SEL %d\n", st->selected);
    if (n < 0 || (size_t)n >= cap - off) return STORE_ERR_FULL;
    off += (size_t)n;

    if (out_len) *out_len = off;
    return STORE_OK;
}

static int parse_int(const char *s, const char *end, int *out) {
    int neg = 0, any = 0;
    long v = 0;
    if (s < end && *s == '-') { neg = 1; ++s; }
    for (; s < end; ++s) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (*s - '0');
        any = 1;
    }
    if (!any) return -1;
    *out = neg ? (int)-v : (int)v;
    return 0;
}

static int parse_u64(const char *s, const char *end, uint64_t *out) {
    char tmp[32];
    size_t n = (size_t)(end - s);
    if (!out || n == 0 || n >= sizeof tmp) return -1;
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    char *stop = NULL;
    unsigned long long v = strtoull(tmp, &stop, 10);
    if (stop != tmp + n) return -1;
    *out = (uint64_t)v;
    return 0;
}

store_status_t store_deserialize(store_t *st, const char *buf, size_t len) {
    if (!st || !buf) return STORE_ERR_ARG;
    store_init(st);

    int want_sel = -1;
    uint64_t want_expire[STORE_MAX_SUBS];
    int expire_seen[STORE_MAX_SUBS];
    memset(want_expire, 0, sizeof want_expire);
    memset(expire_seen, 0, sizeof expire_seen);
    const char *p = buf;
    const char *end = buf + len;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *le = nl ? nl : end;
        size_t llen = (size_t)(le - p);

        if (llen >= 4 && memcmp(p, "SUB ", 4) == 0) {
            const char *rest = p + 4;
            int fixed_idx = -1;
            const char *sp1 = memchr(rest, ' ', (size_t)(le - rest));
            if (sp1) {
                int try_idx = -1;
                if (parse_int(rest, sp1, &try_idx) == 0 &&
                    try_idx >= 0 && try_idx < STORE_MAX_SUBS) {
                    const char *after = sp1 + 1;
                    const char *sp2 = memchr(after, ' ', (size_t)(le - after));
                    if (sp2 && after[0] &&
                        (after[0] == 'h' || after[0] == 'H')) {
                        fixed_idx = try_idx;
                        rest = after;
                        sp1 = sp2;
                    }
                }
                char url[512], name[64];
                size_t ul = (size_t)(sp1 - rest);
                size_t nl2 = (size_t)(le - sp1 - 1);
                if (ul < sizeof url && nl2 < sizeof name && ul > 0) {
                    memcpy(url, rest, ul); url[ul] = '\0';
                    memcpy(name, sp1 + 1, nl2); name[nl2] = '\0';
                    if (fixed_idx >= 0 && !st->subs[fixed_idx].used) {
                        store_sub_t *s = &st->subs[fixed_idx];
                        memcpy(s->name, name, nl2); s->name[nl2] = '\0';
                        memcpy(s->url, url, ul); s->url[ul] = '\0';
                        s->used = 1;
                    } else {
                        size_t sub;
                        store_add_sub(st, name, url, &sub);
                    }
                }
            }
        } else if (llen >= 8 && memcmp(p, "SUBMETA ", 8) == 0) {
            const char *rest = p + 8;
            const char *sp = memchr(rest, ' ', (size_t)(le - rest));
            if (sp) {
                int idx = -1;
                uint64_t expire = 0;
                if (parse_int(rest, sp, &idx) == 0 &&
                    parse_u64(sp + 1, le, &expire) == 0 &&
                    idx >= 0 && idx < STORE_MAX_SUBS) {
                    want_expire[idx] = expire;
                    expire_seen[idx] = 1;
                }
            }
        } else if (llen >= 6 && memcmp(p, "ORDER ", 6) == 0) {
            const char *q = p + 6;
            st->section_n = 0;
            while (q < le && st->section_n < STORE_MAX_SUBS + 1) {
                while (q < le && *q == ' ') ++q;
                if (q >= le) break;
                const char *sp = memchr(q, ' ', (size_t)(le - q));
                if (!sp) sp = le;
                int id = 0;
                if (parse_int(q, sp, &id) == 0)
                    st->section_order[st->section_n++] = id;
                q = sp;
            }
        } else if (llen >= 4 && memcmp(p, "SRV ", 4) == 0) {
            const char *rest = p + 4;
            const char *sp = memchr(rest, ' ', (size_t)(le - rest));
            if (sp) {
                int grp;
                if (parse_int(rest, sp, &grp) == 0) {
                    char link[2048];
                    size_t ll = (size_t)(le - sp - 1);
                    if (ll < sizeof link && st->n < STORE_MAX_SERVERS) {
                        memcpy(link, sp + 1, ll); link[ll] = '\0';
                        if (cfg_parse_link(link, &st->servers[st->n]) == CFG_OK) {
                            st->group[st->n] = grp;
                            st->n++;
                        }
                    }
                }
            }
        } else if (llen >= 4 && memcmp(p, "SEL ", 4) == 0) {
            parse_int(p + 4, le, &want_sel);
        }

        if (!nl) break;
        p = nl + 1;
    }

    store_normalize(st);
    for (size_t i = 0; i < STORE_MAX_SUBS; ++i)
        if (expire_seen[i] && st->subs[i].used)
            st->subs[i].expire = want_expire[i];

    if (want_sel >= 0 && (size_t)want_sel < st->n) st->selected = want_sel;
    else st->selected = -1;
    return STORE_OK;
}
