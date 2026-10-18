#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include <stdint.h>
#include "rules.h"

#ifdef __cplusplus
extern "C" {
#endif

/* retain unknown transports so validation can return a stable reason */
typedef enum {
    VL_NET_TCP = 0,
    VL_NET_WS,
    VL_NET_GRPC,
    VL_NET_HTTP,
    VL_NET_XHTTP,
    VL_NET_UNKNOWN
} vl_net_t;

typedef enum {
    VL_SEC_NONE = 0,
    VL_SEC_TLS,
    VL_SEC_REALITY,
    VL_SEC_UNKNOWN
} vl_sec_t;

typedef enum {
    VL_PROTO_VLESS = 0,
    VL_PROTO_SOCKS5,
    VL_PROTO_HTTP,
    VL_PROTO_HTTPS,
    VL_PROTO_TROJAN,
    VL_PROTO_SHADOWSOCKS,
/* quic only: no tcp transport carries it. senko-core runs it on ios 12 and
   later, and the utun backend dials it through daemon/hy2_engine.c everywhere
   else. see daemon_ctl_apply's CTL_ACT_START */
    VL_PROTO_HYSTERIA2,
/* the aead header only: the uuid lives in uuid, the body cipher ("auto",
   "aes-128-gcm", "chacha20-poly1305") in encryption */
    VL_PROTO_VMESS,
/* a feed entry senko cannot run (tuic, ssr, a bad field), kept as a named row
   so the list shows everything the panel sent. never dialed */
    VL_PROTO_UNSUPPORTED
} vl_proto_t;

/* vless and vmess name their user by uuid; every dial path parses it for both */
static inline int cfg_proto_uses_uuid(vl_proto_t p) {
    return p == VL_PROTO_VLESS || p == VL_PROTO_VMESS;
}

/* fixed storage avoids heap ownership ambiguity on old ios */
typedef struct {
    vl_proto_t proto;
    char     user[64];
/* trojan and hysteria2 panels hand out long random tokens as passwords */
    char     pass[256];

    char     uuid[64];
    char     host[256];
    uint16_t port;

    vl_sec_t security;
    vl_net_t net;

    char     sni[256];
    char     ws_host[256];
    char     flow[32];
    char     encryption[32];
    char     fp[32];
    char     pbk[128];
    char     sid[32];
    char     path[256];
    char     mode[16]; /* preserve xhttp mode for transport selection */
/* xray's grpc transport registers two distinct stream names, "Tun" and
   "TunMulti"; cfg_normalize_grpc_path needs to know which one the server
   expects before it can build a path the server will recognize */
    int      grpc_multi;
    char     remark[256];
/* skip certificate and hostname verification for this server's tls
   connection. plenty of trojan/vless nodes run behind a bare ip or a
   self-signed cert and rely on the client honoring this instead of
   presenting a real chain */
    int      insecure;
/* hysteria2 fields. senko-core removed allowInsecure entirely, so a
   self-signed node has to be pinned by hash instead of waved through */
    char     pin_sha256[128];
/* hysteria2 obfuscation: only "salamander" reaches senko-core's finalmask
   udp mask (transport/internet/finalmask/salamander), so any other value
   fails validation instead of connecting unobfuscated */
    char     obfs[32];
    char     obfs_password[128];
/* hysteria2 "multi-port" hop list, kept verbatim ("123,5000-6000"): the go
   core dials one port from it at random and rotates through the rest
   (transport/internet/hysteria/udphop). empty when the link names one port */
    char     port_hop[128];
/* why a VL_PROTO_UNSUPPORTED row cannot run, in the import stats wording */
    char     unsupported[64];
} vl_server_t;

typedef enum {
    CFG_OK            =  0,
    CFG_ERR_BAD_ARG   = -1,
    CFG_ERR_SCHEME    = -2,
    CFG_ERR_NO_AT     = -3,
    CFG_ERR_NO_HOST   = -4,
    CFG_ERR_BAD_PORT  = -5,
    CFG_ERR_TOO_LONG  = -6,
    CFG_ERR_NO_MEMORY = -7,
    /* a well formed link senko has no client for; the reason says which */
    CFG_ERR_UNSUPPORTED = -8
} cfg_status_t;

int url_percent_decode_ex(const char *src, size_t src_len, char *dst,
                          size_t cap, int plus_is_space);
int url_percent_decode(const char *src, size_t src_len, char *dst, size_t cap);

/* keep names stable because the control protocol exposes them */
const char *vl_sec_name(vl_sec_t s);

cfg_status_t cfg_parse_link(const char *uri, vl_server_t *out);

/* the same, with why set to a short name of what could not be carried
   ("tuic", "httpupgrade transport", "shadowsocks plugin obfs-local") when the
   result is CFG_ERR_UNSUPPORTED. core/share_link.c */
cfg_status_t cfg_parse_link_ex(const char *uri, vl_server_t *out, char *why, size_t why_cap);

/* the base64 json body of a v2rayn vmess:// link, everything after the scheme
   up to a '#' or '?'. core/config.c, called from the share link parser */
cfg_status_t cfg_parse_vmess_json(const char *body, size_t len, vl_server_t *out,
                                  char *why, size_t why_cap);

/* 1 when h can be a dns name: an sni or a tls host */
int cfg_sni_text_ok(const char *h);

/* grpc wire path from a serviceName, as xray builds it ("/name/Tun") */
void cfg_normalize_grpc_path(vl_server_t *s);

/* hysteria2's "multi-port" hop list ("123,5000-6000"): validates the charset
   and every port/range, copies the field verbatim into dst (may be NULL to
   only validate), and reports the first port for callers that still need one
   fixed port number */
int cfg_parse_port_hop(const char *text, size_t len, char *dst, size_t dst_cap,
                       uint16_t *first_port_out);

/* config files store one rule after the SET rule prefix */
rules_status_t cfg_parse_rule(const char *text, size_t len, rule_t *out);

/* validate after parsing so unsupported combinations fail explicitly */
int cfg_validate_server(const vl_server_t *s, char *reason, size_t reason_cap);

int cfg_validate_link(const char *uri, char *reason, size_t reason_cap);

/* what a pasted or imported body actually is. the ui needs the distinction to
   say "unknown content type" instead of "no servers found" */
typedef enum {
    CFG_CONTENT_UNKNOWN = 0,
    CFG_CONTENT_LINKS,      /* one or more scheme://... lines */
    CFG_CONTENT_BASE64,     /* v2ray style base64 wrapped link list */
    CFG_CONTENT_XRAY_JSON,  /* xray / v2rayn exported config */
    CFG_CONTENT_HAPP,       /* happ:// bundle */
    CFG_CONTENT_CLASH,      /* clash / clash-meta yaml */
    CFG_CONTENT_SURGE       /* shadowrocket / surge ini */
} cfg_content_t;

cfg_content_t cfg_content_kind(const char *blob, size_t blob_len);

/* skip malformed entries so one stale node does not hide valid nodes */
cfg_status_t cfg_parse_subscription(const char *blob, size_t blob_len,
                                    vl_server_t *out, size_t max_servers,
                                    size_t *out_count);

/* what an import left out, grouped by the reason, most common first. a public
   feed skips a dozen kinds, and the one that dominates may come last */
#define CFG_SKIP_KINDS 16
typedef struct {
    size_t skipped;
    struct {
        char what[64];
        size_t count;
    } kinds[CFG_SKIP_KINDS];
    size_t other; /* skips beyond the kinds kept above */
/* set only by cfg_parse_subscription_rows */
    int keep_unsupported;
} cfg_import_stats_t;

cfg_status_t cfg_parse_subscription_ex(const char *blob, size_t blob_len,
                                       vl_server_t *out, size_t max_servers,
                                       size_t *out_count, cfg_import_stats_t *stats);

/* the same, but each skipped node that still names a host or a title is also
   kept in out as a VL_PROTO_UNSUPPORTED row, so a subscription list shows
   everything its panel sent. manual imports use the _ex form */
cfg_status_t cfg_parse_subscription_rows(const char *blob, size_t blob_len,
                                         vl_server_t *out, size_t max_servers,
                                         size_t *out_count, cfg_import_stats_t *stats);


/* how share links, xray and sing-box json and clash profiles all read a node,
   so one format cannot accept what another refuses. the two mappers return -1
   with why naming what senko cannot carry ("httpupgrade transport") */
int  cfg_node_transport(vl_server_t *s, const char *name, const char *header,
                        char *why, size_t why_cap);
int  cfg_node_security(vl_server_t *s, const char *name, char *why, size_t why_cap);
void cfg_node_vless_id(vl_server_t *s, const char *id);
void cfg_node_ss_method(vl_server_t *s, const char *method);
/* the defaults every format shares: sni follows the host, a websocket host
   follows the sni, the grpc path, vision spellings, quic and shadowsocks
   security */
void cfg_node_finish(vl_server_t *s);

/* "skipped 3: tuic 2, ssr 1", or "" when nothing was skipped. the four most
   common kinds are named, the rest summed as "other" */
void cfg_import_stats_text(const cfg_import_stats_t *stats, char *out, size_t cap);

/* counts one skipped node under what */
void cfg_import_stats_add(cfg_import_stats_t *stats, const char *what);

/* counts one skipped node and, inside cfg_parse_subscription_rows, appends it
   to out as a VL_PROTO_UNSUPPORTED row. an entry with neither a remark nor a
   host has nothing to show and is only counted */
void cfg_import_skip(cfg_import_stats_t *stats, vl_server_t *out, size_t max,
                     size_t *count, const char *remark, const char *host,
                     uint16_t port, const char *what);

/* a happ deep link usually carries the panel's subscription url and nothing
   else. that url is not a proxy senko can dial, so the importer has to
   register it as a subscription rather than reject it as an unusable node.
   returns 0 and fills out when the body is exactly one such url */
int cfg_subscription_url(const char *blob, size_t blob_len,
                         char *out, size_t out_cap);

/* why a fetched body carried no node senko can run, when the answer is known.
   returns a static string or NULL when there is nothing specific to say */
const char *cfg_reject_reason(const char *blob, size_t blob_len,
                              const char *source_url);

#ifdef __cplusplus
}
#endif

#endif /* config_h */
