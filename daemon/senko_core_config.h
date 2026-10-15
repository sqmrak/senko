#ifndef SENKO_CORE_CONFIG_H
#define SENKO_CORE_CONFIG_H

#include "core/config.h"

#include <stddef.h>

/* render the selected profile without exposing its secrets to argv or logs */
int senko_core_config_render(const vl_server_t *server, const char *endpoint_ip,
                     const char *ifname,
                     char *out, size_t out_cap);

int senko_core_config_render_rules(const vl_server_t *server, const char *endpoint_ip,
                           const char *ifname, const ruleset_t *rules, const char *geo_dir,
                           const char *ca_file, char *out, size_t out_cap);

/* geo_dir holds the geosite.dat and geoip.dat xray reads, NULL when the caller
   cannot give senko-core one (the packet tunnel extension has no such path) */

/* how many rules senko-core cannot take: app rules always, geo rules when it has
   no dat to read. the utun backend decides those itself, so a profile that uses
   them prefers it */
size_t senko_core_config_unsupported(const ruleset_t *rules, const char *geo_dir);

#endif
