#ifndef NET_SAFE_H
#define NET_SAFE_H

#include <stddef.h>
#include <stdint.h>

struct sockaddr;

int net_hostname_safe(const char *s);

int net_url_host_safe(const char *s);

int net_url_path_safe(const char *s, size_t len);

int net_addr_allowed(const struct sockaddr *sa);

/* resolve once, reject the whole answer if any address is not globally routable */
int net_resolve_public(const char *host, uint16_t port, char *numeric, size_t cap);

/* reject private ipv4 literals early so dial never sees them */
int net_ipv4_host_allowed(const char *host);

int net_ipv4_literal(const char *s, char *out, size_t cap);

/* exact-token membership over a comma or space separated address list; strstr
   treats 1.2.3.4 as present inside 11.2.3.40 and skips a required bypass rule */
int net_ip_list_contains(const char *list, const char *ip);

#endif
