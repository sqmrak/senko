#define _DEFAULT_SOURCE

#include "app_owner.h"

#include <arpa/inet.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/plist_bundle_id.h"
#include "core/senko_time.h"

/* the parts of xnu's sys/proc_info.h this needs. the header is private to the
   ios sdks, so the layouts are written out and the size is checked: a layout
   that drifted would read the port from the wrong bytes and name the wrong app */
#define PROC_ALL_PIDS 1
#define PROC_INFO_CALL_LISTPIDS 1
#define PROC_INFO_CALL_PIDINFO 2
#define PROC_INFO_CALL_PIDFDINFO 3
#define PROC_PIDLISTFDS 1
#define PROC_PIDPATHINFO 11
#define PROC_PIDFDSOCKETINFO 3
#define PROX_FDTYPE_SOCKET 2
#define SOCKINFO_IN 1
#define SOCKINFO_TCP 2
#define INI_IPV4 1
#define INI_IPV6 2
#define PATH_MAX_BYTES 4096

typedef struct {
    int32_t  proc_fd;
    uint32_t proc_fdtype;
} xn_fdinfo_t;

typedef struct {
    uint8_t  pfi[24];       /* proc_fileinfo */
    uint8_t  vstat[136];    /* vinfo_stat */
    uint64_t soi_so;
    uint64_t soi_pcb;
    int32_t  soi_type;
    int32_t  soi_protocol;
    int32_t  soi_family;
    int16_t  soi_flags[8];  /* options, linger, state, qlen, incqlen, qlimit, timeo, error */
    uint32_t soi_oobmark;
    uint8_t  soi_rcv[24];
    uint8_t  soi_snd[24];
    int32_t  soi_kind;
    uint32_t rfu_1;
    uint64_t soi_proto[66]; /* the union of the per kind socket infos, 528 bytes */
} xn_socket_fdinfo_t;

typedef char xn_socket_fdinfo_size_check[sizeof(xn_socket_fdinfo_t) == 792 ? 1 : -1];

/* offsets inside in_sockinfo, which starts the union (tcp_sockinfo begins with it) */
#define INI_LPORT 4
#define INI_VFLAG 24
#define INI_LADDR 48

#define TABLE_MAX 4096
#define PID_MAX 8192
#define FD_BYTES_MAX (256 * 1024)
#define FD_BYTES_DEFAULT (32 * 1024)
#define SCAN_MIN_MS 50     /* a miss may rescan, but not faster than this */
#define SCAN_STALE_MS 1000 /* a hit this old is read again before it is trusted */
#define PATH_CACHE 64

typedef struct {
    uint8_t  protocol; /* TUN_FLOW_TCP or TUN_FLOW_UDP */
    uint8_t  addr_len; /* 0 for a wildcard, else 4 or 16 */
    uint16_t port;
    int32_t  pid;
    uint8_t  addr[16];
} entry_t;

typedef struct {
    char path[256];
    char bundle[128];
    int  found;
    uint64_t used;
} path_cache_t;

struct app_owner {
    app_owner_info_fn info;
    entry_t *table;
    size_t count;
    uint64_t scanned_ms;
    int scanned;
    path_cache_t cache[PATH_CACHE];
    uint64_t clock;
    app_owner_stats_t stats;
};

app_owner_t *app_owner_open(app_owner_info_fn info) {
    app_owner_t *o;
    if (!info) {
        void *sym = dlsym(RTLD_DEFAULT, "__proc_info");
        if (!sym) return NULL;
        /* a data pointer from dlsym converted to the function it names */
        memcpy(&info, &sym, sizeof info);
    }
    o = (app_owner_t *)calloc(1, sizeof *o);
    if (!o) return NULL;
    o->table = (entry_t *)calloc(TABLE_MAX, sizeof *o->table);
    if (!o->table) {
        free(o);
        return NULL;
    }
    o->info = info;
    return o;
}

void app_owner_close(app_owner_t *o) {
    if (!o) return;
    free(o->table);
    free(o);
}

void app_owner_stats(const app_owner_t *o, app_owner_stats_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (o) *out = o->stats;
}

static void add_socket(app_owner_t *o, int pid, const xn_socket_fdinfo_t *si) {
    const uint8_t *in = (const uint8_t *)si->soi_proto;
    entry_t e;
    uint8_t vflag;
    int32_t lport;
    if (si->soi_kind != SOCKINFO_IN && si->soi_kind != SOCKINFO_TCP) return;
    if (si->soi_protocol != 6 && si->soi_protocol != 17) return;
    memcpy(&lport, in + INI_LPORT, sizeof lport);
    vflag = in[INI_VFLAG];
    if (lport <= 0 || lport > 0xffff) return;
    memset(&e, 0, sizeof e);
    e.protocol = si->soi_protocol == 6 ? TUN_FLOW_TCP : TUN_FLOW_UDP;
    /* the kernel keeps the port in network order */
    e.port = ntohs((uint16_t)lport);
    e.pid = pid;
    if (vflag & INI_IPV4) {
        /* an ipv4 address sits in the last word of the 16 byte union */
        memcpy(e.addr, in + INI_LADDR + 12, 4);
        e.addr_len = (e.addr[0] | e.addr[1] | e.addr[2] | e.addr[3]) ? 4 : 0;
    } else if (vflag & INI_IPV6) {
        memcpy(e.addr, in + INI_LADDR, 16);
        {
            uint8_t any = 0;
            for (int i = 0; i < 16; ++i) any |= e.addr[i];
            e.addr_len = any ? 16 : 0;
        }
    }
    if (o->count < TABLE_MAX) o->table[o->count++] = e;
}

static void scan(app_owner_t *o, uint64_t now_ms) {
    int *pids = (int *)malloc(PID_MAX * sizeof *pids);
    uint8_t *fds = (uint8_t *)malloc(FD_BYTES_MAX);
    if (!pids || !fds) {
        free(pids);
        free(fds);
        return;
    }
    o->count = 0;
    o->scanned = 1;
    o->scanned_ms = now_ms;
    ++o->stats.scans;
    int bytes = o->info(PROC_INFO_CALL_LISTPIDS, PROC_ALL_PIDS, 0, 0, pids,
                        (int)(PID_MAX * sizeof *pids));
    if (bytes <= 0) {
        ++o->stats.errors;
        o->stats.sockets = 0;
        free(pids);
        free(fds);
        return;
    }
    int npids = bytes / (int)sizeof *pids;
    for (int i = 0; i < npids && o->count < TABLE_MAX; ++i) {
        int pid = pids[i];
        if (pid <= 0) continue;
        /* a null buffer asks how many bytes the list needs; a kernel that will not
           say gets a buffer that holds a few thousand descriptors */
        int need = o->info(PROC_INFO_CALL_PIDINFO, pid, PROC_PIDLISTFDS, 0, NULL, 0);
        if (need == 0) continue;
        if (need < 0) need = FD_BYTES_DEFAULT;
        if (need > FD_BYTES_MAX) need = FD_BYTES_MAX;
        int got = o->info(PROC_INFO_CALL_PIDINFO, pid, PROC_PIDLISTFDS, 0, fds, need);
        if (got <= 0) {
            if (got < 0) ++o->stats.errors;
            continue;
        }
        int nfds = got / (int)sizeof(xn_fdinfo_t);
        for (int f = 0; f < nfds && o->count < TABLE_MAX; ++f) {
            xn_fdinfo_t fd;
            xn_socket_fdinfo_t si;
            memcpy(&fd, fds + (size_t)f * sizeof fd, sizeof fd);
            if (fd.proc_fdtype != PROX_FDTYPE_SOCKET) continue;
            memset(&si, 0, sizeof si);
            if (o->info(PROC_INFO_CALL_PIDFDINFO, pid, PROC_PIDFDSOCKETINFO,
                        (uint64_t)(uint32_t)fd.proc_fd, &si, (int)sizeof si) != (int)sizeof si) {
                /* the socket closed between the listing and the query */
                continue;
            }
            add_socket(o, pid, &si);
        }
    }
    o->stats.sockets = o->count;
    free(pids);
    free(fds);
}

/* the entry that holds the flow's port: one bound to the flow's own address
   beats one bound to every address */
static const entry_t *find_entry(const app_owner_t *o, const tun_flow_key_t *key) {
    const entry_t *wildcard = NULL;
    for (size_t i = 0; i < o->count; ++i) {
        const entry_t *e = &o->table[i];
        if (e->protocol != key->protocol || e->port != key->source_port) continue;
        if (e->addr_len == 0) {
            if (!wildcard) wildcard = e;
            continue;
        }
        if (e->addr_len == key->address_len && memcmp(e->addr, key->source, e->addr_len) == 0)
            return e;
    }
    return wildcard;
}

/* the outermost .app of an executable path: an extension inside an app counts
   as the app, because that is the identifier a person picked */
static int app_dir(const char *path, char *out, size_t cap) {
    const char *p = path;
    while ((p = strstr(p, ".app")) != NULL) {
        if (p[4] == '/' || p[4] == '\0') {
            size_t n = (size_t)(p + 4 - path);
            if (n + 1 > cap) return -1;
            memcpy(out, path, n);
            out[n] = '\0';
            return 0;
        }
        p += 4;
    }
    return -1;
}

static int read_plist(const char *dir, uint8_t *buf, size_t cap, size_t *len) {
    char file[PATH_MAX_BYTES + 16];
    int n = snprintf(file, sizeof file, "%s/Info.plist", dir);
    FILE *f;
    if (n <= 0 || (size_t)n >= sizeof file) return -1;
    f = fopen(file, "rb");
    if (!f) return -1;
    *len = fread(buf, 1, cap, f);
    fclose(f);
    return *len > 0 ? 0 : -1;
}

static int bundle_of_path(app_owner_t *o, const char *path, char *bundle, size_t cap) {
    path_cache_t *slot = NULL;
    for (size_t i = 0; i < PATH_CACHE; ++i) {
        if (o->cache[i].used && strcmp(o->cache[i].path, path) == 0) {
            o->cache[i].used = ++o->clock;
            if (!o->cache[i].found || strlen(o->cache[i].bundle) >= cap) return 0;
            memcpy(bundle, o->cache[i].bundle, strlen(o->cache[i].bundle) + 1);
            return 1;
        }
        if (!slot || o->cache[i].used < slot->used) slot = &o->cache[i];
    }
    if (strlen(path) >= sizeof slot->path) return 0;
    memset(slot, 0, sizeof *slot);
    memcpy(slot->path, path, strlen(path) + 1);
    slot->used = ++o->clock;
    {
        char dir[PATH_MAX_BYTES];
        static uint8_t plist[64 * 1024];
        size_t len = 0;
        if (app_dir(path, dir, sizeof dir) == 0 && read_plist(dir, plist, sizeof plist, &len) == 0 &&
            plist_bundle_id(plist, len, slot->bundle, sizeof slot->bundle) > 0)
            slot->found = 1;
    }
    if (!slot->found || strlen(slot->bundle) >= cap) return 0;
    memcpy(bundle, slot->bundle, strlen(slot->bundle) + 1);
    return 1;
}

int app_owner_lookup(app_owner_t *o, const tun_flow_key_t *key, uint64_t now_ms,
                     char *bundle, size_t cap) {
    const entry_t *e;
    char path[PATH_MAX_BYTES];
    int n;
    if (bundle && cap) bundle[0] = '\0';
    if (!o || !key || !bundle || cap < 2) return 0;
    ++o->stats.lookups;
    if (!o->scanned || now_ms - o->scanned_ms > SCAN_STALE_MS || now_ms < o->scanned_ms)
        scan(o, now_ms);
    e = find_entry(o, key);
    if (!e && now_ms - o->scanned_ms >= SCAN_MIN_MS) {
        /* the app may have opened the socket after the last pass */
        scan(o, now_ms);
        e = find_entry(o, key);
    }
    if (!e) {
        ++o->stats.unowned;
        return 0;
    }
    memset(path, 0, sizeof path);
    n = o->info(PROC_INFO_CALL_PIDINFO, e->pid, PROC_PIDPATHINFO, 0, path, (int)sizeof path - 1);
    if (n <= 0) {
        ++o->stats.errors;
        return 0;
    }
    if (!bundle_of_path(o, path, bundle, cap)) {
        ++o->stats.no_bundle;
        return 0;
    }
    ++o->stats.named;
    return 1;
}

int app_owner_policy_lookup(void *ctx, const tun_flow_key_t *key, char *bundle, size_t cap) {
    return app_owner_lookup((app_owner_t *)ctx, key, (uint64_t)senko_now_ms(), bundle, cap);
}
