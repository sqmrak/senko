#ifndef SETTINGS_H
#define SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#include "core/dns_msg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_DNS_UPSTREAM_MAX 64
#define SENKO_DEFAULT_SOCKS_PORT 11080
#define SENKO_DEFAULT_DNS_LOCAL_PORT 10053
/* a phone that lost its carrier for good must stop redialling it, and five
   attempts already cover the 31 seconds of backoff a handover needs */
#define SENKO_DEFAULT_RECONNECT_ATTEMPTS 5

/* the longest key and value the control protocol carries in one SET */
#define SETTINGS_KEY_MAX   32
#define SETTINGS_VALUE_MAX 64

/* which rung of the backend ladder the developer screen pinned. the ladder
   picks on its own at AUTO, which is what every normal install runs */
typedef enum {
    SENKO_BACKEND_AUTO = 0,
    SENKO_BACKEND_GO,
    SENKO_BACKEND_C,
    SENKO_BACKEND_APP_PROXY
} senko_backend_force_t;

/* -1 keeps the eight variant ladder, anything else pins one variant and fails
   instead of walking on, because a rung that only works after five rejections
   is exactly what a tester needs to be able to name */
#define SENKO_PF_MODE_AUTO (-1)

/* the two pins travel together because they are read from the same settings and
   the backend ladder consults both */
typedef struct {
    senko_backend_force_t backend;
    int                   pf_mode;
} senko_force_t;

typedef struct {
    uint16_t socks_port;
    int      socks_public;
    uint16_t dns_local_port;
    char     dns_upstream[SETTINGS_DNS_UPSTREAM_MAX];
    dns_block_response_t block_response;
    /* what the daemon does on its own, with no client attached */
    int      auto_connect; /* redial the stored selection at startup */
    int      auto_reconnect; /* redial after the data path drops */
    int      reconnect_max_attempts; /* 0 retries until it works */
    int      sub_refresh_hours; /* 0 turns scheduled refreshes off */
    int      failover; /* walk the current section when a server will not come up */

/* the developer overrides. they exist because the ladders below them choose
   silently, and a tester who cannot pin a rung cannot tell which one broke */
    senko_backend_force_t force_backend;
    int      force_pf_mode;
    int      sub_ignore_gating; /* take the panel's placeholder feed anyway */
    int      trace; /* per connection event lines on the daemon log */
} daemon_settings_t;

typedef enum {
    SETTINGS_OK        =  0,
    SETTINGS_ERR_KEY   = -1, /* a key this build does not know */
    SETTINGS_ERR_VALUE = -2  /* a known key with a value it cannot take */
} settings_status_t;

void daemon_settings_defaults(daemon_settings_t *s);

/* the word the control protocol and the config file use for a pinned backend */
const char *daemon_settings_backend_name(senko_backend_force_t forced);

/* the one place that maps a key to a field, shared by the config file and the
   control protocol so both accept exactly the same set */
settings_status_t daemon_settings_set(daemon_settings_t *s,
                                      const char *key, size_t key_len,
                                      const char *val, size_t val_len);

int daemon_settings_apply_line(daemon_settings_t *s, const char *line, size_t len);
void daemon_settings_apply_buf(daemon_settings_t *s, const char *buf, size_t len);
int daemon_settings_serialize(const daemon_settings_t *s, char *buf, size_t cap,
                              size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* settings_h */
