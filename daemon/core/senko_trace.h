#ifndef SENKO_TRACE_H
#define SENKO_TRACE_H

#include "session.h"

void senko_trace_set_th_label(void *th, const char *host);
void senko_trace_sess(session_t *s, const char *event, const char *detail);
void senko_trace_th(void *th, const char *event, const char *detail);
void session_trace_close(session_t *s, const char *reason);
void session_set_trace_host(session_t *s, const vless_dest_t *dest);

#endif