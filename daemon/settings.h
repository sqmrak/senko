#ifndef SETTINGS_H
#define SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_DNS_UPSTREAM_MAX 64
#define SENKO_DEFAULT_SOCKS_PORT 11080
#define SENKO_DEFAULT_DNS_LOCAL_PORT 10053

typedef struct {
    uint16_t socks_port;
    int      socks_public;
    uint16_t dns_local_port;
    char     dns_upstream[SETTINGS_DNS_UPSTREAM_MAX];
} daemon_settings_t;

void daemon_settings_defaults(daemon_settings_t *s);

int daemon_settings_apply_line(daemon_settings_t *s, const char *line, size_t len);
void daemon_settings_apply_buf(daemon_settings_t *s, const char *buf, size_t len);
int daemon_settings_serialize(const daemon_settings_t *s, char *buf, size_t cap,
                              size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* settings_h */
