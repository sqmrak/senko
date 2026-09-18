#ifndef NET_SAFE_H
#define NET_SAFE_H

#include <stddef.h>
#include <stdint.h>

struct sockaddr;
struct addrinfo;

/* bounded getaddrinfo: a lookup that outlives timeout_ms is abandoned instead
   of blocking the caller, because a censored network can silently drop dns
   traffic and leave a plain getaddrinfo() call hanging indefinitely. returns
   0 and a result the caller must freeaddrinfo(), or nonzero otherwise. */
int net_getaddrinfo_timed(const char *host, const char *service,
                          const struct addrinfo *hints,
                          struct addrinfo **res, int timeout_ms);

/* general bounded-worker primitive net_getaddrinfo_timed is built on: runs
   work(arg) on a detached thread and waits up to timeout_ms. returns nonzero
   and *out_result = work()'s return value on completion (now owned by the
   caller); returns 0 on timeout, leaving the worker to run to completion in
   the background and free its own result through free_result, since nobody
   is left to claim it. exposed so the timeout/abandon bookkeeping can be
   exercised directly with a synthetic slow worker, without depending on real,
   unpredictably slow DNS in a test. */
int net_run_bounded(void *(*work)(void *arg), void *arg,
                    void (*free_result)(void *result),
                    int timeout_ms, void **out_result);

int net_hostname_safe(const char *s);

int net_url_host_safe(const char *s);

int net_url_path_safe(const char *s, size_t len);

int net_addr_allowed(const struct sockaddr *sa);

/* resolve once, reject the whole answer if any address is not globally routable */
int net_resolve_public(const char *host, uint16_t port, char *numeric, size_t cap);

/* direct probes use ipv4 because the routing bypass has no ipv6 rule */
int net_resolve_public_ipv4(const char *host, uint16_t port, char *numeric, size_t cap);

/* reject private ipv4 literals early so dial never sees them */
int net_ipv4_host_allowed(const char *host);

int net_ipv4_literal(const char *s, char *out, size_t cap);

/* exact-token membership over a comma or space separated address list; strstr
   treats 1.2.3.4 as present inside 11.2.3.40 and skips a required bypass rule */
int net_ip_list_contains(const char *list, const char *ip);

#endif
