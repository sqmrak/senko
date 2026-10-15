#ifndef NET_DIAG_H
#define NET_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "net_fail.h"
#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* how far a probe of one server got before it failed */
typedef enum {
    NET_DIAG_OK = 0,
    NET_DIAG_DNS,         /* the server name did not resolve */
    NET_DIAG_TRANSPORT,   /* tcp, tls, reality or the http transport failed; see transport */
    NET_DIAG_CLOSED,      /* the server closed the proxied request without answering */
    NET_DIAG_SILENT,      /* the server took the request and never answered */
    NET_DIAG_ANSWER,      /* the server's answer was wrong; see answer */
    NET_DIAG_NOT_PROXIED  /* the probe target answered http_status instead of 204 */
} net_diag_stage_t;

typedef struct {
    net_diag_stage_t stage;
    net_fail_t       transport;
    sess_fail_t      answer;
    int              http_status;
    vl_proto_t       proto;
    uint16_t         port;
    int              clock_known;
    int64_t          clock_offset_s; /* device minus server */
} net_diag_t;

/* a device clock this far off breaks vmess, which allows two minutes, and
   reality servers that check the time */
#define NET_DIAG_CLOCK_LIMIT_S 120

/* whether a better answer needs the device clock, which the caller can then
   measure before asking for the text */
int net_diag_wants_clock(const net_diag_t *d);

/* one english sentence for the user: what is wrong and what to check. the app
   translates these, so their wording is part of the control protocol */
void net_diag_text(const net_diag_t *d, char *out, size_t cap);

/* an estimated clock difference from an unauthenticated http response */
void net_diag_clock_text(int64_t offset_s, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* NET_DIAG_H */
