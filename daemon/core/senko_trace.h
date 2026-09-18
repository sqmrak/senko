#ifndef SENKO_TRACE_H
#define SENKO_TRACE_H

#include "session.h"

/* event tracing is a runtime flag, not a build flag: one line per session open,
   transport choice and close costs one predictable branch per connection, and
   it is the only way to answer "why did it drop" from a shipped build. the
   per byte counters and the replay and upload buffers stay behind
   SENKO_RELEASE, because those are 64 kb per session */
void senko_trace_set_enabled(int on);
int  senko_trace_enabled(void);

void senko_trace_set_th_label(void *th, const char *host);
void senko_trace_sess(session_t *s, const char *event, const char *detail);
void senko_trace_th(void *th, const char *event, const char *detail);
void session_trace_close(session_t *s, const char *reason);
void session_set_trace_host(session_t *s, const vless_dest_t *dest);

#endif