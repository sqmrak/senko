#ifndef ROUTING_H
#define ROUTING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* reserve a stable rule range so cleanup cannot remove foreign rules */
#define ROUTING_IPFW_BASE      12000
#define ROUTING_IPFW_FWD       12020
#define ROUTING_MAX_RULES      16

typedef enum {
    ROUTING_OK        =  0,
    ROUTING_ERR_ARG   = -1,
    ROUTING_ERR_SPACE = -2 /* a buffer was too small */
} routing_status_t;

/* keep both syntax variants because older ipfw rejects the trailing keyword */
typedef struct {
    int  number;
    char rule_out[320];
    char rule_plain[320];
} routing_ipfw_rule_t;

/* generate ordered ipfw rules with server and loopback bypasses first */
routing_status_t routing_ipfw_rules(const char *server_ip,
                                    int redir_port, int socks_port,
                                    int dns_local_port,
                                    routing_ipfw_rule_t *rules, size_t cap,
                                    size_t *out_count);

/* enumerate syntax variants because ios pf versions accept different keywords */
typedef enum {
    ROUTING_PF_ROUTE_TO_LO0 = 0, /* use route-to syntax with nat and rdr */
    ROUTING_PF_ROUTE_TO_LO0_NOGW, /* retry route-to without gateway syntax */
    ROUTING_PF_DIVERT_TO, /* use modern divert-to placement */
    ROUTING_PF_DIVERT_TO_OLD, /* retry legacy divert-to placement */
    ROUTING_PF_RDR_TO, /* use rdr-to with state flags */
    ROUTING_PF_RDR_TO_OLD, /* retry rdr-to with keep state */
    ROUTING_PF_LEGACY_RDR, /* use the oldest rdr form */
    ROUTING_PF_COMPAT_RDR /* fall back when extended pf grammar is unavailable */
} routing_pf_mode_t;

#define ROUTING_PF_MODE_COUNT 8
#define ROUTING_MAX_IFS       8

/* generate a bounded pf ruleset with all resolved server bypasses */
routing_status_t routing_pf_conf(const char *server_ips,
                                 const char ifnames[][32], size_t if_count,
                                 int redir_port, int dns_local_port,
                                 routing_pf_mode_t mode,
                                 char *buf, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* routing_h */
