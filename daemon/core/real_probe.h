#ifndef REAL_PROBE_H
#define REAL_PROBE_H

#include <stddef.h>

#include "config.h"
#include "net_diag.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the host every probe asks through the server. it answers 204 with no body
   and is reachable from every region a panel sells nodes in */
#define REAL_PROBE_HOST "www.gstatic.com"
#define REAL_PROBE_PORT 80

/* lets the caller pin the probe socket to the physical interface before it
   connects, so a running tunnel cannot answer the handshake itself */
typedef void (*real_probe_bind_fn)(int fd);

/* the delay a user actually waits for: tcp to the server, its tls or reality
   handshake, the proxy request and one http answer from REAL_PROBE_HOST
   carried back through it. a tcp connect alone measures the nearest cdn edge
   when a node sits behind one, which read as 2 ms for a server abroad.
   ip is the server's resolved ipv4 address. returns milliseconds, or -1 with
   reason saying what is wrong in words a user can act on; diag (may be NULL)
   keeps the facts behind it, so a caller that measures the clock afterwards
   can word it again with net_diag_text */
int real_probe_run(const vl_server_t *server, const char *ip,
                   real_probe_bind_fn bind_fd, int timeout_ms,
                   net_diag_t *diag, char *reason, size_t reason_cap);

/* a bare tcp check of the server port, with the same socket diagnosis */
int real_probe_tcp(const vl_server_t *server, const char *ip,
                   real_probe_bind_fn bind_fd, int timeout_ms,
                   char *reason, size_t reason_cap);

/* the status of an http answer's first line, or -1 when it is not one */
int real_probe_status(const char *buf, size_t len);

/* asks REAL_PROBE_HOST at ip directly, outside any tunnel, and records its
   Date in senko_clock; 0 when it did */
int real_probe_clock(const char *ip, real_probe_bind_fn bind_fd, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* real_probe_h */
