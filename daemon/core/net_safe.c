#define _DEFAULT_SOURCE

#include "net_safe.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

int net_hostname_safe(const char *s) {
    if (!s || !s[0]) return 0;
    for (const char *p = s; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x21 || c >= 0x7f) return 0;
        if (c == ' ' || c == '\t' || c == '/' || c == '?' || c == ':' ||
            c == '%' || c == '\\')
            return 0;
    }
    return 1;
}

int net_url_host_safe(const char *s) {
    struct in6_addr ip6;
    if (s && inet_pton(AF_INET6, s, &ip6) == 1) return 1;
    return net_hostname_safe(s);
}

int net_url_path_safe(const char *s, size_t len) {
    if (!s) return 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7f) return 0;
        if (c == '\r' || c == '\n') return 0;
    }
    return 1;
}

int net_addr_allowed(const struct sockaddr *sa) {
    if (!sa) return 0;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        if ((ip >> 24) == 0) return 0;
        if ((ip >> 24) == 10) return 0;
        if ((ip >> 24) == 127) return 0;
        if ((ip & 0xffc00000u) == 0x64400000u) return 0;
        if ((ip >> 20) == 0xac1) return 0;
        if ((ip >> 16) == 0xa9fe) return 0;
        if ((ip >> 16) == 0xc0a8) return 0;
        if ((ip & 0xffffff00u) == 0xc0000000u) return 0;
        if ((ip & 0xffffff00u) == 0xc0000200u) return 0;
        if ((ip & 0xffffff00u) == 0xc0586300u) return 0;
        if ((ip & 0xfffe0000u) == 0xc6120000u) return 0;
        if ((ip & 0xffffff00u) == 0xc6336400u) return 0;
        if ((ip & 0xffffff00u) == 0xcb007100u) return 0;
        if ((ip >> 24) >= 224) return 0;
        return 1;
    }
    if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
        const unsigned char *a = sin6->sin6_addr.s6_addr;
        static const unsigned char loopback[16] =
            { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 };
        static const unsigned char zero[16] = { 0 };
        if (memcmp(a, zero, 16) == 0) return 0;
        if (memcmp(a, loopback, 16) == 0) return 0;
        if (a[0] == 0xfe && (a[1] & 0xc0) == 0x80) return 0;
        if ((a[0] & 0xfe) == 0xfc) return 0;
        if (a[0] == 0xfe && (a[1] & 0xc0) == 0xc0) return 0;
        if (a[0] == 0xff) return 0;
        if (a[0] == 0x20 && a[1] == 0x01 && a[2] == 0x0d && a[3] == 0xb8)
            return 0;
        if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
            struct sockaddr_in mapped;
            memset(&mapped, 0, sizeof mapped);
            mapped.sin_family = AF_INET;
            memcpy(&mapped.sin_addr, a + 12, 4);
            return net_addr_allowed((const struct sockaddr *)&mapped);
        }
        return 1;
    }
    return 0;
}

/* a blocking call that outlives the caller's patience is handed off to its
   own detached worker rather than joined, so the caller never waits past
   timeout_ms no matter how long the real work takes. whichever side notices
   the other's flag last (the worker finishing, or the waiter timing out) is
   the one that tears the job down, so neither a slow worker nor an impatient
   caller can free state the other still touches. */
typedef void *(*net_bounded_work_fn)(void *arg);
typedef void (*net_bounded_free_fn)(void *result);

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    int             done;
    int             abandoned;
    void           *result;
} net_bounded_job_t;

typedef struct {
    net_bounded_job_t  *job;
    net_bounded_work_fn work;
    void                *arg;
    net_bounded_free_fn free_result;
} net_bounded_args_t;

static void *net_bounded_worker(void *p) {
    net_bounded_args_t *a = (net_bounded_args_t *)p;
    void *result = a->work(a->arg);

    pthread_mutex_lock(&a->job->lock);
    int abandoned = a->job->abandoned;
    if (!abandoned) {
        a->job->result = result;
        a->job->done = 1;
        pthread_cond_signal(&a->job->cond);
    }
    pthread_mutex_unlock(&a->job->lock);

    if (abandoned) {
        if (result && a->free_result) a->free_result(result);
        pthread_mutex_destroy(&a->job->lock);
        pthread_cond_destroy(&a->job->cond);
        free(a->job);
    }
    free(a);
    return NULL;
}

/* runs work(arg) on a detached worker and waits up to timeout_ms for it to
   finish. on completion, *out_result receives work()'s return value (the
   caller now owns it). on timeout, returns 0 and the worker keeps running in
   the background, freeing its own result through free_result once it
   finishes since nobody is left to claim it. arg must be safe to hand to
   another thread (typically a value already owned by the caller, or a fresh
   heap allocation the worker frees itself, as net_resolve_do does below). */
int net_run_bounded(net_bounded_work_fn work, void *arg,
                    net_bounded_free_fn free_result,
                    int timeout_ms, void **out_result) {
    if (out_result) *out_result = NULL;
    net_bounded_job_t *job = (net_bounded_job_t *)calloc(1, sizeof *job);
    if (!job) return 0;
    if (pthread_mutex_init(&job->lock, NULL) != 0) { free(job); return 0; }
    if (pthread_cond_init(&job->cond, NULL) != 0) {
        pthread_mutex_destroy(&job->lock);
        free(job);
        return 0;
    }

    net_bounded_args_t *args = (net_bounded_args_t *)calloc(1, sizeof *args);
    if (!args) {
        pthread_mutex_destroy(&job->lock);
        pthread_cond_destroy(&job->cond);
        free(job);
        return 0;
    }
    args->job = job;
    args->work = work;
    args->arg = arg;
    args->free_result = free_result;

    pthread_t th;
    if (pthread_create(&th, NULL, net_bounded_worker, args) != 0) {
        pthread_mutex_destroy(&job->lock);
        pthread_cond_destroy(&job->cond);
        free(job);
        free(args);
        return 0;
    }
    pthread_detach(th);

/* clock_gettime/CLOCK_REALTIME is not available before iOS 10, and
   pthread_cond_timedwait's default clock matches gettimeofday() on every
   target this daemon runs on (no condattr clock override in use), so the
   deadline is built from that instead */
    struct timeval now;
    gettimeofday(&now, NULL);
    struct timespec deadline;
    deadline.tv_sec = now.tv_sec + timeout_ms / 1000;
    deadline.tv_nsec = (long)now.tv_usec * 1000L +
                       (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_nsec -= 1000000000L;
        deadline.tv_sec += 1;
    }

    pthread_mutex_lock(&job->lock);
    while (!job->done) {
        if (pthread_cond_timedwait(&job->cond, &job->lock, &deadline) == ETIMEDOUT)
            break;
    }
    int ok = job->done;
    if (ok && out_result) *out_result = job->result;
    if (!ok) job->abandoned = 1;
    pthread_mutex_unlock(&job->lock);

    if (ok) {
        pthread_mutex_destroy(&job->lock);
        pthread_cond_destroy(&job->cond);
        free(job);
    }
    /* !ok: the worker frees job (and the result it still produces) itself */
    return ok;
}

typedef struct {
    char host[256];
    char service[16];
    struct addrinfo hints;
} net_resolve_call_t;

typedef struct {
    int rc;
    struct addrinfo *res;
} net_resolve_result_t;

static void *net_resolve_do(void *arg) {
    net_resolve_call_t *c = (net_resolve_call_t *)arg;
    net_resolve_result_t *r = (net_resolve_result_t *)malloc(sizeof *r);
    if (r) {
        r->res = NULL;
        r->rc = getaddrinfo(c->host[0] ? c->host : NULL,
                            c->service[0] ? c->service : NULL, &c->hints, &r->res);
    }
    free(c);
    return r;
}

static void net_resolve_free_result(void *p) {
    net_resolve_result_t *r = (net_resolve_result_t *)p;
    if (r->rc == 0 && r->res) freeaddrinfo(r->res);
    free(r);
}

int net_getaddrinfo_timed(const char *host, const char *service,
                          const struct addrinfo *hints,
                          struct addrinfo **out_res, int timeout_ms) {
    if (!out_res) return -1;
    *out_res = NULL;
    net_resolve_call_t *call = (net_resolve_call_t *)calloc(1, sizeof *call);
    if (!call) return -1;
    if (host) snprintf(call->host, sizeof call->host, "%s", host);
    if (service) snprintf(call->service, sizeof call->service, "%s", service);
    if (hints) call->hints = *hints;

    void *out = NULL;
    if (!net_run_bounded(net_resolve_do, call, net_resolve_free_result,
                         timeout_ms, &out))
        return -1; /* timed out; net_resolve_do frees call once it eventually runs */
    if (!out) return -1; /* malloc failed inside the worker */
    net_resolve_result_t *r = (net_resolve_result_t *)out;
    int rc = r->rc;
    *out_res = r->res; /* ownership passes to the caller */
    free(r);
    return rc;
}

int net_resolve_public(const char *host, uint16_t port, char *numeric, size_t cap) {
    struct addrinfo hints, *res = NULL;
    char service[8];
    int found = 0;
    if (!host || !host[0] || !numeric || cap == 0 || port == 0) return 0;
    numeric[0] = '\0';
    snprintf(service, sizeof service, "%u", (unsigned)port);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (net_getaddrinfo_timed(host, service, &hints, &res, 2000) != 0 || !res) return 0;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        const void *src = NULL;
        if (ai->ai_family == AF_INET)
            src = &((const struct sockaddr_in *)ai->ai_addr)->sin_addr;
        else if (ai->ai_family == AF_INET6)
            src = &((const struct sockaddr_in6 *)ai->ai_addr)->sin6_addr;
        else
            continue;
        found = 1;
        if (!net_addr_allowed(ai->ai_addr)) {
            numeric[0] = '\0';
            freeaddrinfo(res);
            return 0;
        }
        if (!numeric[0] && !inet_ntop(ai->ai_family, src, numeric, (socklen_t)cap)) {
            freeaddrinfo(res);
            return 0;
        }
    }
    freeaddrinfo(res);
    return found && numeric[0];
}

int net_resolve_public_ipv4(const char *host, uint16_t port, char *numeric, size_t cap) {
    struct addrinfo hints, *res = NULL;
    char service[8];
    int found = 0;
    if (!host || !host[0] || !numeric || cap == 0 || port == 0) return 0;
    numeric[0] = '\0';
    snprintf(service, sizeof service, "%u", (unsigned)port);
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (net_getaddrinfo_timed(host, service, &hints, &res, 2000) != 0 || !res) return 0;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET) continue;
        found = 1;
        if (!net_addr_allowed(ai->ai_addr) ||
            (!numeric[0] && !inet_ntop(AF_INET,
                                        &((const struct sockaddr_in *)ai->ai_addr)->sin_addr,
                                        numeric, (socklen_t)cap))) {
            numeric[0] = '\0';
            freeaddrinfo(res);
            return 0;
        }
    }
    freeaddrinfo(res);
    return found && numeric[0];
}

int net_ipv4_host_allowed(const char *host) {
    if (!host || !host[0]) return 0;
    struct in_addr a;
    if (inet_pton(AF_INET, host, &a) != 1)
        return 1; /* hostname: resolve-time filter applies */
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    sin.sin_addr = a;
    return net_addr_allowed((struct sockaddr *)&sin);
}

int net_ipv4_literal(const char *s, char *out, size_t cap) {
    if (!s || !out || cap < 8) return 0;
    struct in_addr a;
    if (inet_pton(AF_INET, s, &a) != 1) return 0;
    if (!inet_ntop(AF_INET, &a, out, (socklen_t)cap)) return 0;
    return 1;
}

int net_ip_list_contains(const char *list, const char *ip) {
    if (!list || !ip || !ip[0]) return 0;
    size_t iplen = strlen(ip);
    for (const char *p = list; *p; ) {
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
        const char *start = p;
        while (*p && *p != ',' && *p != ' ' && *p != '\t') p++;
        if ((size_t)(p - start) == iplen && memcmp(start, ip, iplen) == 0)
            return 1;
    }
    return 0;
}
