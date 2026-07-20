#ifndef NET_SAFE_H
#define NET_SAFE_H

#include <stddef.h>

struct sockaddr;

int net_hostname_safe(const char *s);

int net_url_host_safe(const char *s);

int net_url_path_safe(const char *s, size_t len);

int net_addr_allowed(const struct sockaddr *sa);

/* reject private ipv4 literals early so dial never sees them */
int net_ipv4_host_allowed(const char *host);

int net_ipv4_literal(const char *s, char *out, size_t cap);

#endif