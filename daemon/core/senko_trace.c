#include "senko_trace.h"
#include "session.h"
#include "vless.h"

#ifndef SENKO_RELEASE
#include "senko_replay.h"
#include "senko_upload.h"
#endif

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#define TRACE_LABEL_MAX 280

typedef struct {
    void *th;
    char  label[TRACE_LABEL_MAX];
} th_trace_t;

#define TH_TRACE_MAX 64
static th_trace_t g_th_trace[TH_TRACE_MAX];

/* a debug build traced unconditionally before this flag existed, and that is
   what its users expect. a release build starts quiet and is turned on from
   the developer screen, because the lines are worth one branch per connection
   but not a log nobody asked for */
#ifdef SENKO_RELEASE
static int g_trace_enabled = 0;
#else
static int g_trace_enabled = 1;
#endif

void senko_trace_set_enabled(int on) {
    g_trace_enabled = on ? 1 : 0;
}

int senko_trace_enabled(void) {
    return g_trace_enabled;
}

static unsigned long trace_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)tv.tv_sec * 1000ul + (unsigned long)tv.tv_usec / 1000ul;
}

static const char *host_or_dash(const char *host) {
    return (host && host[0]) ? host : "-";
}

static th_trace_t *th_slot(void *th) {
    if (!th) return NULL;
    for (size_t i = 0; i < TH_TRACE_MAX; ++i) {
        if (g_th_trace[i].th == th) return &g_th_trace[i];
    }
    for (size_t i = 0; i < TH_TRACE_MAX; ++i) {
        if (g_th_trace[i].th == NULL) {
            g_th_trace[i].th = th;
            g_th_trace[i].label[0] = '\0';
            return &g_th_trace[i];
        }
    }
    return NULL;
}

void senko_trace_set_th_label(void *th, const char *host) {
    th_trace_t *slot = th_slot(th);
    if (!slot) return;
    if (!host) host = "";
    snprintf(slot->label, sizeof slot->label, "%s", host);
}

static const char *th_label(void *th) {
    for (size_t i = 0; i < TH_TRACE_MAX; ++i) {
        if (g_th_trace[i].th == th) return host_or_dash(g_th_trace[i].label);
    }
    return "-";
}

/* the byte counters only exist where they are compiled in, so a release build
   prints the event and the vision framing state and stops there */
static void trace_line(const char *host, const void *ctx, const session_t *s,
                       const char *event, const char *detail) {
    int ds = s ? s->vision_downstream_direct : 0;
    int us = s ? s->vision_upstream_direct : 0;
    int us_pend = s ? s->vision_upstream_direct_pending : 0;
#ifndef SENKO_RELEASE
    if (s) {
        fprintf(stderr,
                "senko-trace: t=%lu host=%s ctx=%p event=%s %s "
                "app_tx=%llu app_rx=%llu wire_tx=%llu wire_rx=%llu "
                "ds=%d us=%d us_pend=%d\n",
                trace_ms(), host_or_dash(host), ctx,
                event ? event : "?", detail ? detail : "",
                (unsigned long long)s->trace_app_tx,
                (unsigned long long)s->trace_app_rx,
                (unsigned long long)s->trace_wire_tx,
                (unsigned long long)s->trace_wire_rx,
                ds, us, us_pend);
        fflush(stderr);
        return;
    }
#endif
    fprintf(stderr,
            "senko-trace: t=%lu host=%s ctx=%p event=%s %s ds=%d us=%d us_pend=%d\n",
            trace_ms(), host_or_dash(host), ctx,
            event ? event : "?", detail ? detail : "", ds, us, us_pend);
    fflush(stderr);
}

void senko_trace_sess(session_t *s, const char *event, const char *detail) {
    if (!s || !g_trace_enabled) return;
    trace_line(s->trace_host, s, s, event, detail);
}

void senko_trace_th(void *th, const char *event, const char *detail) {
    if (!g_trace_enabled) return;
    trace_line(th_label(th), th, NULL, event, detail);
}

void session_set_trace_host(session_t *s, const vless_dest_t *dest) {
    if (!s || !dest) return;
    if (dest->atyp == VLESS_ADDR_DOMAIN) {
        snprintf(s->trace_host, sizeof s->trace_host, "%s:%u",
                 dest->domain, (unsigned)dest->port);
    } else if (dest->atyp == VLESS_ADDR_IPV4) {
        snprintf(s->trace_host, sizeof s->trace_host,
                 "%u.%u.%u.%u:%u",
                 dest->host_addr[0], dest->host_addr[1],
                 dest->host_addr[2], dest->host_addr[3],
                 (unsigned)dest->port);
    } else if (dest->atyp == VLESS_ADDR_IPV6) {
        char tmp[64];
        snprintf(tmp, sizeof tmp,
                 "[%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
                 "%02x%02x:%02x%02x:%02x%02x:%02x%02x]:%u",
                 dest->host_addr[0], dest->host_addr[1],
                 dest->host_addr[2], dest->host_addr[3],
                 dest->host_addr[4], dest->host_addr[5],
                 dest->host_addr[6], dest->host_addr[7],
                 dest->host_addr[8], dest->host_addr[9],
                 dest->host_addr[10], dest->host_addr[11],
                 dest->host_addr[12], dest->host_addr[13],
                 dest->host_addr[14], dest->host_addr[15],
                 (unsigned)dest->port);
        snprintf(s->trace_host, sizeof s->trace_host, "%s", tmp);
    } else {
        snprintf(s->trace_host, sizeof s->trace_host, "?:%u", (unsigned)dest->port);
    }
    senko_trace_set_th_label(s->th, s->trace_host);
}

void session_trace_close(session_t *s, const char *reason) {
    if (!s || s->trace_closed) return;
    s->trace_closed = 1;
    char detail[96];
    snprintf(detail, sizeof detail, "state=%d %s",
             (int)s->state, reason ? reason : "");
    senko_trace_sess(s, "session_close", detail);
#ifndef SENKO_RELEASE
    senko_replay_flush(s);
    senko_upload_flush(s);
#endif
    if (s->th) {
        for (size_t i = 0; i < TH_TRACE_MAX; ++i) {
            if (g_th_trace[i].th == s->th) {
                g_th_trace[i].th = NULL;
                g_th_trace[i].label[0] = '\0';
                break;
            }
        }
    }
}
