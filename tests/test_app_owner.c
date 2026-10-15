#define _DEFAULT_SOURCE

#include "app_owner.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "plist_fixture.inc"

static int failures;

static void ok(const char *name, int condition) {
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s\n", name);
}

/* a kernel of a few processes, each holding a few descriptors */
typedef struct {
    int pid;
    char path[300];
    int sockets;
    struct {
        int fd;
        int type;       /* 2 socket, 1 vnode */
        int kind;       /* 1 in, 2 tcp, 3 un */
        int protocol;
        int port;       /* host order */
        int v6;
        uint8_t addr[16];
    } fd[8];
} fake_proc_t;

static fake_proc_t procs[8];
static int proc_count;
static int fail_list;
static int refuse_null_probe;
static int info_calls, path_calls;
static int path_answers_zero; /* ios 6 writes the path and answers 0 */

static fake_proc_t *proc_of(int pid) {
    for (int i = 0; i < proc_count; ++i)
        if (procs[i].pid == pid) return &procs[i];
    return NULL;
}

static int fake_info(int callnum, int pid, int flavor, uint64_t arg, void *buffer, int size) {
    ++info_calls;
    if (callnum == 1) {
        if (fail_list) return -1;
        int *out = buffer;
        int n = 0;
        for (int i = 0; i < proc_count && (n + 1) * (int)sizeof *out <= size; ++i) out[n++] = procs[i].pid;
        return n * (int)sizeof *out;
    }
    fake_proc_t *p = proc_of(pid);
    if (!p) return -1;
    if (callnum == 2 && flavor == 1) {
        if (!buffer) return refuse_null_probe ? -1 : p->sockets * 8;
        int32_t *out = buffer;
        int n = 0;
        for (int i = 0; i < p->sockets && (n + 1) * 8 <= size; ++i, ++n) {
            out[n * 2] = p->fd[i].fd;
            out[n * 2 + 1] = (int32_t)p->fd[i].type;
        }
        return n * 8;
    }
    if (callnum == 2 && flavor == 11) {
        ++path_calls;
        strncpy(buffer, p->path, (size_t)size);
        return path_answers_zero ? 0 : (int)strlen(p->path);
    }
    if (callnum == 3 && flavor == 3) {
        for (int i = 0; i < p->sockets; ++i) {
            if (p->fd[i].fd != (int)arg) continue;
            if (size != 792) return -1;
            uint8_t *raw = buffer;
            memset(raw, 0, 792);
            /* soi_protocol at 180, soi_kind at 256, the union at 264 */
            int32_t protocol = p->fd[i].protocol, kind = p->fd[i].kind;
            memcpy(raw + 180, &protocol, 4);
            memcpy(raw + 256, &kind, 4);
            int32_t lport = (int32_t)htons((uint16_t)p->fd[i].port);
            memcpy(raw + 264 + 4, &lport, 4);
            raw[264 + 24] = p->fd[i].v6 ? 2 : 1;
            if (p->fd[i].v6) memcpy(raw + 264 + 48, p->fd[i].addr, 16);
            else memcpy(raw + 264 + 48 + 12, p->fd[i].addr, 4);
            return 792;
        }
        return -1;
    }
    return -1;
}

static void add_proc(int pid, const char *path) {
    fake_proc_t *p = &procs[proc_count++];
    memset(p, 0, sizeof *p);
    p->pid = pid;
    snprintf(p->path, sizeof p->path, "%s", path);
}

static void add_sock(int pid, int fd, int kind, int protocol, int port, const uint8_t addr[4]) {
    fake_proc_t *p = proc_of(pid);
    int i = p->sockets++;
    p->fd[i].fd = fd;
    p->fd[i].type = 2;
    p->fd[i].kind = kind;
    p->fd[i].protocol = protocol;
    p->fd[i].port = port;
    if (addr) memcpy(p->fd[i].addr, addr, 4);
}

static tun_flow_key_t key(int protocol, int port, const uint8_t src[4]) {
    tun_flow_key_t k;
    memset(&k, 0, sizeof k);
    k.protocol = (uint8_t)protocol;
    k.source_port = (uint16_t)port;
    k.address_len = 4;
    memcpy(k.source, src, 4);
    return k;
}

static void write_app(const char *root, const char *name, const unsigned char *plist, size_t len,
                      char *exe, size_t cap) {
    char dir[400], file[460];
    snprintf(dir, sizeof dir, "%s/%s.app", root, name);
    mkdir(dir, 0700);
    snprintf(file, sizeof file, "%s/Info.plist", dir);
    FILE *f = fopen(file, "wb");
    fwrite(plist, 1, len, f);
    fclose(f);
    snprintf(exe, cap, "%s/%s", dir, name);
}

int main(void) {
    char root[] = "/tmp/senko-owner-XXXXXX";
    char browser[420], player[420], ext[500], daemon_path[] = "/usr/libexec/nsurlsessiond";
    const uint8_t utun[4] = { 10, 8, 0, 2 }, other_if[4] = { 192, 168, 1, 9 };
    char bundle[128];
    app_owner_stats_t st;
    if (!mkdtemp(root)) return 2;

    write_app(root, "Browser", kPlistBinary, sizeof kPlistBinary, browser, sizeof browser);
    write_app(root, "Player", kPlistXml, sizeof kPlistXml, player, sizeof player);
    snprintf(ext, sizeof ext, "%s/Browser.app/PlugIns/Share.appex/Share", root);

    add_proc(10, browser);
    add_proc(20, player);
    add_proc(30, daemon_path);
    add_proc(40, ext);
    add_sock(10, 3, 2, 6, 50001, utun);          /* browser tcp from the utun address */
    add_sock(10, 4, 1, 17, 50002, NULL);         /* browser udp, bound to every address */
    add_sock(20, 3, 2, 6, 50003, utun);
    add_sock(20, 4, 3, 0, 50004, utun);          /* a unix socket: ignored */
    add_sock(30, 3, 2, 6, 50005, utun);          /* a daemon outside any app */
    add_sock(40, 3, 2, 6, 50006, utun);          /* an extension of the browser */
    add_sock(20, 5, 2, 6, 50007, other_if);      /* the player on another interface */
    add_sock(10, 6, 2, 6, 50007, utun);          /* the same port on the utun address */
    add_sock(20, 7, 2, 6, 0, utun);              /* port zero: ignored */
    add_proc(50, browser);
    procs[proc_count - 1].fd[0].fd = 3;
    procs[proc_count - 1].fd[0].type = 1;        /* a vnode, not a socket */
    procs[proc_count - 1].sockets = 1;

    app_owner_t *o = app_owner_open(fake_info);
    ok("open", o != NULL);
    ok("no lookup without keys", app_owner_lookup(o, NULL, 1000, bundle, sizeof bundle) == 0);

    tun_flow_key_t k = key(TUN_FLOW_TCP, 50001, utun);
    ok("tcp socket of an app", app_owner_lookup(o, &k, 1000, bundle, sizeof bundle) == 1 &&
       strcmp(bundle, "com.example.Demo-App_1") == 0);
    k = key(TUN_FLOW_TCP, 50003, utun);
    ok("xml plist app", app_owner_lookup(o, &k, 1001, bundle, sizeof bundle) == 1 &&
       strcmp(bundle, "com.example.Demo-App_1") == 0);
    k = key(TUN_FLOW_UDP, 50002, utun);
    ok("udp socket bound to every address", app_owner_lookup(o, &k, 1002, bundle, sizeof bundle) == 1);
    k = key(TUN_FLOW_TCP, 50002, utun);
    ok("the protocol must match", app_owner_lookup(o, &k, 1003, bundle, sizeof bundle) == 0);
    k = key(TUN_FLOW_TCP, 50004, utun);
    ok("a unix socket is not a flow", app_owner_lookup(o, &k, 1004, bundle, sizeof bundle) == 0);
    k = key(TUN_FLOW_TCP, 50005, utun);
    ok("a daemon outside an app has no bundle", app_owner_lookup(o, &k, 1005, bundle, sizeof bundle) == 0);
    k = key(TUN_FLOW_TCP, 50006, utun);
    ok("an extension is its app", app_owner_lookup(o, &k, 1006, bundle, sizeof bundle) == 1 &&
       strcmp(bundle, "com.example.Demo-App_1") == 0);
    /* ios 6.1 answers a path query with 0 and fills the buffer; reading the
       answer as a length made every lookup fail */
    path_answers_zero = 1;
    app_owner_t *old = app_owner_open(fake_info);
    k = key(TUN_FLOW_TCP, 50006, utun);
    ok("a kernel that answers 0 with the path in the buffer", app_owner_lookup(old, &k, 1000, bundle,
       sizeof bundle) == 1 && strcmp(bundle, "com.example.Demo-App_1") == 0);
    char kept_path[sizeof procs[0].path];
    memcpy(kept_path, proc_of(40)->path, sizeof kept_path);
    proc_of(40)->path[0] = '\0';
    app_owner_close(old);
    old = app_owner_open(fake_info);
    app_owner_stats(old, &st);
    ok("an empty path is not an app", app_owner_lookup(old, &k, 1000, bundle, sizeof bundle) == 0);
    app_owner_stats(old, &st);
    ok("and it is counted as an error", st.errors == 1);
    app_owner_close(old);
    memcpy(proc_of(40)->path, kept_path, sizeof kept_path);
    path_answers_zero = 0;

    k = key(TUN_FLOW_TCP, 50007, utun);
    ok("the socket on the flow's own address wins over another interface's",
       app_owner_lookup(o, &k, 1007, bundle, sizeof bundle) == 1);
    k = key(TUN_FLOW_TCP, 0, utun);
    ok("port zero is not held by anyone", app_owner_lookup(o, &k, 1008, bundle, sizeof bundle) == 0);
    app_owner_stats(o, &st);
    ok("stats", st.named == 5 && st.no_bundle == 1 && st.unowned >= 3 && st.sockets == 7 && st.scans >= 1);

    /* a table younger than the stale limit answers hits without another pass, and
       a miss rescans at most every 50 ms */
    app_owner_stats(o, &st);
    uint64_t scans = st.scans;
    k = key(TUN_FLOW_TCP, 50001, utun);
    ok("hit with a fresh table", app_owner_lookup(o, &k, 1500, bundle, sizeof bundle) == 1);
    app_owner_stats(o, &st);
    ok("no rescan for a hit", st.scans == scans);
    k = key(TUN_FLOW_TCP, 59999, utun);
    ok("miss", app_owner_lookup(o, &k, 1520, bundle, sizeof bundle) == 0);
    app_owner_stats(o, &st);
    ok("a miss well after the last pass rescans", st.scans == scans + 1);
    ok("miss again", app_owner_lookup(o, &k, 1530, bundle, sizeof bundle) == 0);
    app_owner_stats(o, &st);
    ok("a miss within 50 ms of a pass does not rescan", st.scans == scans + 1);
    ok("miss later", app_owner_lookup(o, &k, 1600, bundle, sizeof bundle) == 0);
    app_owner_stats(o, &st);
    ok("a miss after 50 ms rescans once", st.scans == scans + 2);

    /* an app that opens its socket after the last pass is found by the rescan */
    add_sock(20, 9, 2, 6, 50100, utun);
    k = key(TUN_FLOW_TCP, 50100, utun);
    ok("socket opened after the pass", app_owner_lookup(o, &k, 1700, bundle, sizeof bundle) == 1);
    /* a table older than a second is read again before it is trusted: the port
       moved to another app and the old answer must not stand */
    procs[0].fd[0].port = 51000;
    k = key(TUN_FLOW_TCP, 50001, utun);
    ok("stale table", app_owner_lookup(o, &k, 3000, bundle, sizeof bundle) == 0);
    k = key(TUN_FLOW_TCP, 51000, utun);
    ok("moved port", app_owner_lookup(o, &k, 3001, bundle, sizeof bundle) == 1);

    /* the plist is read once per executable path */
    char gone[460];
    snprintf(gone, sizeof gone, "%s/Browser.app/Info.plist", root);
    unlink(gone);
    ok("cached plist survives the file", app_owner_lookup(o, &k, 3002, bundle, sizeof bundle) == 1 &&
       strcmp(bundle, "com.example.Demo-App_1") == 0);

    /* failures are counted and answered as unknown, never as a guess */
    fail_list = 1;
    k = key(TUN_FLOW_TCP, 50001, utun);
    uint64_t errors_before;
    app_owner_stats(o, &st);
    errors_before = st.errors;
    ok("a kernel that will not list answers unknown", app_owner_lookup(o, &k, 9000, bundle, sizeof bundle) == 0);
    app_owner_stats(o, &st);
    ok("the failure is counted", st.errors > errors_before);
    fail_list = 0;
    refuse_null_probe = 1;
    k = key(TUN_FLOW_TCP, 50006, utun);
    ok("probe refusal", app_owner_lookup(o, &k, 15000, bundle, sizeof bundle) == 1);
    refuse_null_probe = 0;
    ok("output too small", app_owner_lookup(o, &k, 18000, bundle, 4) == 0);
    ok("no table on null owner", app_owner_lookup(NULL, &k, 1, bundle, sizeof bundle) == 0);

    app_owner_close(o);
    app_owner_close(NULL);

    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf %s", root);
    if (system(cmd) != 0) return 2;
    if (failures) {
        fprintf(stderr, "%d app owner checks failed\n", failures);
        return 1;
    }
    puts("all app owner checks passed");
    return 0;
}
