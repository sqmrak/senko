#include "karing_backup.h"
#include "karing_zip.h"
#include "url.h"
#include "third_party/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <string.h>

static const char *string_field(const cJSON *item, const char *key) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(item, key);
    return cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
}

static int already_has_url(const store_t *store, const char *url) {
    for (size_t i = 0; i < STORE_MAX_SUBS; ++i)
        if (store->subs[i].used && strcmp(store->subs[i].url, url) == 0) return 1;
    return 0;
}

/* cJSON's default nesting limit is too deep for the armv7 daemon stack */
static int json_depth_ok(const char *data, size_t len) {
    unsigned depth = 0, tokens = 0;
    int in_string = 0, escaped = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = data[i];
        if (in_string) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') in_string = 0;
        } else if (c == '"') in_string = 1;
        else if ((c == ',' || c == ':') && ++tokens > 131072) return 0;
        else if (c == '{' || c == '[') {
            if (++depth > 64) return 0;
        } else if (c == '}' || c == ']') {
            if (!depth) return 0;
            --depth;
        }
    }
    return !in_string && !depth;
}

static int has_any_field(const cJSON *object, const char *const *keys, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, keys[i]);
        if (!value || cJSON_IsNull(value) || cJSON_IsFalse(value)) continue;
        if (cJSON_IsNumber(value) && value->valuedouble == 0) continue;
        if (cJSON_IsString(value) && value->valuestring && !value->valuestring[0]) continue;
        if ((cJSON_IsArray(value) || cJSON_IsObject(value)) && !value->child) continue;
        return 1;
    }
    return 0;
}

static int text_field_fits(const cJSON *object, const char *key, size_t cap) {
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!value || cJSON_IsNull(value)) return 1;
    return cJSON_IsString(value) && value->valuestring && strlen(value->valuestring) < cap;
}

/* these options change authentication, trust or the stream; the shared
   sing-box parser cannot preserve them, so importing them must fail */
static int server_shape_supported(const cJSON *server) {
    static const char *const server_fields[] = {
        "detour", "dialer_proxy", "multiplex", "packet_encoding", "udp_over_tcp",
        "uot", "plugin_opts", "plugin_options"
    };
    static const char *const tls_fields[] = {
        "certificate", "certificate_path", "certificate_public_key_sha256", "disable_sni",
        "min_version", "max_version", "cipher_suites", "ech", "fragment", "record_fragment", "alpn"
    };
    static const char *const transport_fields[] = {
        "max_early_data", "early_data_header_name", "heartbeat_period", "idle_timeout",
        "health_check_timeout", "permit_without_stream", "initial_windows_size", "user_agent"
    };
    if (!cJSON_IsObject(server) || has_any_field(server, server_fields, sizeof server_fields / sizeof server_fields[0])) return 0;
    const cJSON *alter = cJSON_GetObjectItemCaseSensitive(server, "alter_id");
    if (alter && (!cJSON_IsNumber(alter) || alter->valuedouble != 0)) return 0;
    const cJSON *tls = cJSON_GetObjectItemCaseSensitive(server, "tls");
    if (tls) {
        if (!cJSON_IsObject(tls) || has_any_field(tls, tls_fields, sizeof tls_fields / sizeof tls_fields[0])) return 0;
        const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(tls, "enabled");
        const cJSON *insecure = cJSON_GetObjectItemCaseSensitive(tls, "insecure");
        if (!cJSON_IsBool(enabled) || (insecure && !cJSON_IsBool(insecure)) ||
            !text_field_fits(tls, "server_name", sizeof ((vl_server_t *)0)->sni) ||
            !text_field_fits(tls, "serverName", sizeof ((vl_server_t *)0)->sni)) return 0;
        const cJSON *reality = cJSON_GetObjectItemCaseSensitive(tls, "reality");
        const cJSON *utls = cJSON_GetObjectItemCaseSensitive(tls, "utls");
        static const char *const reality_fields[] = { "spider_x", "spiderX" };
        if (reality && (!cJSON_IsObject(reality) ||
            !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(reality, "enabled")) ||
            has_any_field(reality, reality_fields, sizeof reality_fields / sizeof reality_fields[0]) ||
            !text_field_fits(reality, "public_key", sizeof ((vl_server_t *)0)->pbk) ||
            !text_field_fits(reality, "publicKey", sizeof ((vl_server_t *)0)->pbk) ||
            !text_field_fits(reality, "short_id", sizeof ((vl_server_t *)0)->sid) ||
            !text_field_fits(reality, "shortId", sizeof ((vl_server_t *)0)->sid))) return 0;
        if (utls && (!cJSON_IsObject(utls) ||
            !cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(utls, "enabled")) ||
            !text_field_fits(utls, "fingerprint", sizeof ((vl_server_t *)0)->fp))) return 0;
        if (reality && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reality, "enabled")) &&
            !cJSON_IsTrue(enabled)) return 0;
        const char *type = string_field(server, "type");
        if (!strcmp(type, "socks") || !strcmp(type, "socks5")) return 0;
        if (!strcmp(type, "http")) {
            static const char *const http_tls_fields[] = { "server_name", "insecure", "utls", "reality" };
            if (has_any_field(tls, http_tls_fields, sizeof http_tls_fields / sizeof http_tls_fields[0])) return 0;
        }
    }
    const cJSON *transport = cJSON_GetObjectItemCaseSensitive(server, "transport");
    if (transport && (!cJSON_IsObject(transport) || has_any_field(transport, transport_fields,
                      sizeof transport_fields / sizeof transport_fields[0]))) return 0;
    return 1;
}

int karing_import(store_t *store, const uint8_t *data, size_t len,
                  karing_import_result_t *result) {
    char *json = NULL;
    size_t json_len = 0;
    cJSON *root = NULL;
    store_t *candidate = NULL;
    karing_import_result_t counts = {0, 0, 0};
    int rc = -1;
    if (result) memset(result, 0, sizeof *result);
    if (!store || !result || karing_zip_subscriptions(data, len, &json, &json_len) != 0)
        return -1;
    if (memchr(json, '\0', json_len) || !json_depth_ok(json, json_len)) goto done;
    root = cJSON_ParseWithLengthOpts(json, json_len + 1, NULL, 1);
    if (!cJSON_IsObject(root)) goto done;
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsArray(items) || cJSON_GetArraySize(items) > STORE_MAX_SUBS + 1) goto done;
    candidate = malloc(sizeof *candidate);
    if (!candidate) goto done;
    memcpy(candidate, store, sizeof *candidate);
    const cJSON *group;
    cJSON_ArrayForEach(group, items) {
        static const char *const group_fields[] = {
            "decryptPasswordAES", "decryptPasswordAGE", "userAgentCompatible",
            "userAgentCompatibles", "xhwid", "outboundDns", "preferOutboundDns"
        };
        if (!cJSON_IsObject(group) || has_any_field(group, group_fields,
            sizeof group_fields / sizeof group_fields[0])) goto done;
        const char *url = string_field(group, "urlOrPath");
        if (!url[0]) url = string_field(group, "url");
        const char *id = string_field(group, "groupid");
        const char *name = string_field(group, "remark");
        cJSON *servers = cJSON_GetObjectItemCaseSensitive(group, "servers");
        if (servers && !cJSON_IsArray(servers)) goto done;
        if (servers && cJSON_GetArraySize(servers) > STORE_MAX_SERVERS) goto done;
        cJSON *server;
        cJSON_ArrayForEach(server, servers) {
            if (!server_shape_supported(server)) goto done;
            if (!strcmp(string_field(server, "type"), "socks5")) {
                cJSON *type = cJSON_CreateString("socks");
                if (!type) goto done;
                if (!cJSON_ReplaceItemInObjectCaseSensitive(server, "type", type)) {
                    cJSON_Delete(type);
                    goto done;
                }
            }
        }
        int manual = !url[0] && strcmp(id, "custom") == 0;
        size_t sub = 0;
        if (!manual) {
            url_t parsed;
            if (url_parse(url, &parsed) != URL_OK) { counts.skipped++; continue; }
            if (already_has_url(candidate, url)) { counts.skipped++; continue; }
            if (!name[0]) name = parsed.host;
            if (store_add_sub(candidate, name, url, &sub) != STORE_OK) goto done;
            counts.subscriptions++;
        }
        if (!servers || !cJSON_GetArraySize(servers)) continue;
        cJSON *profile = cJSON_CreateObject();
        if (!profile || !cJSON_AddItemReferenceToObject(profile, "outbounds", (cJSON *)servers)) {
            cJSON_Delete(profile);
            goto done;
        }
        char *body = cJSON_PrintUnformatted(profile);
        cJSON_Delete(profile);
        if (!body) goto done;
        if (manual) {
            vl_server_t *nodes = calloc(STORE_MAX_SERVERS, sizeof *nodes);
            size_t found = 0;
            cfg_import_stats_t stats;
            if (!nodes) { free(body); goto done; }
            (void)cfg_parse_subscription_ex(body, strlen(body), nodes, STORE_MAX_SERVERS, &found, &stats);
            for (size_t i = 0; i < found; ++i) {
                store_status_t status = store_add_manual_server(candidate, &nodes[i], NULL);
                if (status == STORE_OK) counts.servers++;
                else if (status == STORE_ERR_EXISTS) counts.skipped++;
                else { free(nodes); free(body); goto done; }
            }
            counts.skipped += stats.skipped;
            free(nodes);
        } else {
            size_t added = 0;
            cfg_import_stats_t stats;
            store_status_t status = store_refresh_sub_ex(candidate, sub, body, strlen(body),
                                                         &added, &stats, NULL);
            if (status != STORE_OK && status != STORE_ERR_PARSE) { free(body); goto done; }
            counts.servers += added;
            counts.skipped += stats.skipped;
        }
        free(body);
    }
    if (!counts.subscriptions && !counts.servers) goto done;
    memcpy(store, candidate, sizeof *store);
    *result = counts;
    rc = 0;
done:
    free(candidate);
    cJSON_Delete(root);
    free(json);
    return rc;
}

static int add_text(cJSON *object, const char *key, const char *value) {
    return cJSON_AddStringToObject(object, key, value ? value : "") != NULL;
}

static cJSON *export_server(const vl_server_t *s) {
    const char *type = NULL;
    cJSON *o = NULL, *tls, *transport;
    if (s->proto == VL_PROTO_VLESS) type = "vless";
    else if (s->proto == VL_PROTO_VMESS) type = "vmess";
    else if (s->proto == VL_PROTO_TROJAN) type = "trojan";
    else if (s->proto == VL_PROTO_SHADOWSOCKS) type = "shadowsocks";
    else if (s->proto == VL_PROTO_SOCKS5) type = "socks";
    else if (s->proto == VL_PROTO_HTTP || s->proto == VL_PROTO_HTTPS) type = "http";
    else if (s->proto == VL_PROTO_HYSTERIA2) type = "hysteria2";
    if (!type || s->net == VL_NET_XHTTP || s->net == VL_NET_UNKNOWN ||
        s->security == VL_SEC_UNKNOWN || s->pin_sha256[0] || s->port_hop[0] ||
        s->grpc_multi) return NULL;
    int http_proxy = s->proto == VL_PROTO_HTTP || s->proto == VL_PROTO_HTTPS;
    if ((http_proxy || s->proto == VL_PROTO_SOCKS5) && s->net != VL_NET_TCP) return NULL;
    if (s->proto == VL_PROTO_SOCKS5 && s->security != VL_SEC_NONE) return NULL;
    if (http_proxy && (s->insecure || s->fp[0] || s->security == VL_SEC_REALITY ||
        (s->sni[0] && strcmp(s->sni, s->host)))) return NULL;
    o = cJSON_CreateObject();
    if (!o || !add_text(o, "type", type) || !add_text(o, "tag", s->remark) ||
        !add_text(o, "server", s->host) || !cJSON_AddNumberToObject(o, "server_port", s->port)) goto fail;
    if ((s->proto == VL_PROTO_VLESS || s->proto == VL_PROTO_VMESS) &&
        !add_text(o, "uuid", s->uuid)) goto fail;
    if (s->proto == VL_PROTO_VLESS && s->flow[0] && !add_text(o, "flow", s->flow)) goto fail;
    if (s->proto == VL_PROTO_VMESS && !add_text(o, "security", s->encryption[0] ? s->encryption : "auto")) goto fail;
    if (s->proto == VL_PROTO_SHADOWSOCKS && !add_text(o, "method", s->encryption)) goto fail;
    if ((s->proto == VL_PROTO_SOCKS5 || s->proto == VL_PROTO_HTTP || s->proto == VL_PROTO_HTTPS) &&
        s->user[0] && !add_text(o, "username", s->user)) goto fail;
    if (s->pass[0] && !add_text(o, "password", s->pass)) goto fail;
    if (s->security != VL_SEC_NONE || s->proto == VL_PROTO_HTTPS || s->proto == VL_PROTO_TROJAN) {
        tls = cJSON_AddObjectToObject(o, "tls");
        if (!tls || !cJSON_AddBoolToObject(tls, "enabled", s->security != VL_SEC_NONE || s->proto == VL_PROTO_HTTPS)) goto fail;
        if (s->sni[0] && !http_proxy && !add_text(tls, "server_name", s->sni)) goto fail;
        if (s->insecure && !cJSON_AddTrueToObject(tls, "insecure")) goto fail;
        if (s->fp[0]) {
            cJSON *utls = cJSON_AddObjectToObject(tls, "utls");
            if (!utls || !cJSON_AddTrueToObject(utls, "enabled") || !add_text(utls, "fingerprint", s->fp)) goto fail;
        }
        if (s->security == VL_SEC_REALITY) {
            cJSON *reality = cJSON_AddObjectToObject(tls, "reality");
            if (!reality || !cJSON_AddTrueToObject(reality, "enabled") ||
                !add_text(reality, "public_key", s->pbk) || !add_text(reality, "short_id", s->sid)) goto fail;
        }
    }
    if (s->net != VL_NET_TCP) {
        const char *net = s->net == VL_NET_WS ? "ws" : s->net == VL_NET_GRPC ? "grpc" : "http";
        transport = cJSON_AddObjectToObject(o, "transport");
        if (!transport || !add_text(transport, "type", net)) goto fail;
        if (s->net == VL_NET_GRPC) {
            char service[sizeof s->path];
            const char *start = s->path[0] == '/' ? s->path + 1 : s->path;
            snprintf(service, sizeof service, "%s", start);
            size_t n = strlen(service);
            if (n >= 4 && strcmp(service + n - 4, "/Tun") == 0) service[n - 4] = '\0';
            else if (strcmp(service, "Tun") == 0) service[0] = '\0';
            if (!add_text(transport, "service_name", service)) goto fail;
        } else {
            if (s->path[0] && !add_text(transport, "path", s->path)) goto fail;
            if (s->ws_host[0]) {
                if (s->net == VL_NET_WS) {
                    cJSON *headers = cJSON_AddObjectToObject(transport, "headers");
                    if (!headers || !add_text(headers, "Host", s->ws_host)) goto fail;
                } else if (!add_text(transport, "host", s->ws_host)) goto fail;
            }
        }
    }
    if (s->proto == VL_PROTO_HYSTERIA2 && s->obfs[0] && strcmp(s->obfs, "none")) {
        cJSON *obfs = cJSON_AddObjectToObject(o, "obfs");
        if (!obfs || !add_text(obfs, "type", s->obfs) || !add_text(obfs, "password", s->obfs_password)) goto fail;
    }
    return o;
fail:
    cJSON_Delete(o);
    return NULL;
}

static void write16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}

static void write32(uint8_t *p, uint32_t n) {
    write16(p, (uint16_t)n);
    write16(p + 2, (uint16_t)(n >> 16));
}

int karing_export(const store_t *store, uint8_t **data, size_t *len) {
    static const char filename[] = "karing_subscribe.json";
    cJSON *root = NULL, *items = NULL;
    char *json = NULL;
    uint8_t *zip = NULL;
    int rc = -1;
    if (data) *data = NULL;
    if (len) *len = 0;
    if (!store || !data || !len) return -1;
    root = cJSON_CreateObject();
    if (!root || !(items = cJSON_AddArrayToObject(root, "items"))) goto done;
    for (int group = -1; group < STORE_MAX_SUBS; ++group) {
        if (group >= 0 && !store->subs[group].used) continue;
        if (group >= 0 && store->subs[group].header[0]) goto done;
        cJSON *item = cJSON_CreateObject();
        if (!item || !cJSON_AddItemToArray(items, item)) { cJSON_Delete(item); goto done; }
        char id[40];
        if (group < 0) snprintf(id, sizeof id, "custom");
        else snprintf(id, sizeof id, "senko-%d", group);
        if (!add_text(item, "groupid", id) || !add_text(item, "type", "singbox") ||
            !add_text(item, "remark", group < 0 ? "Custom" : store->subs[group].name) ||
            !add_text(item, "urlOrPath", group < 0 ? "" : store->subs[group].url) ||
            !cJSON_AddTrueToObject(item, "enable")) goto done;
        cJSON *servers = cJSON_AddArrayToObject(item, "servers");
        if (!servers) goto done;
        for (size_t i = 0; i < store->n; ++i) {
            if (store->group[i] != group) continue;
            cJSON *server = export_server(&store->servers[i]);
            if (!server || !cJSON_AddItemToArray(servers, server)) { cJSON_Delete(server); goto done; }
        }
    }
    json = cJSON_PrintUnformatted(root);
    if (!json) goto done;
    size_t size = strlen(json), name_len = sizeof filename - 1;
    if (size > 4U * 1024U * 1024U) goto done;
    size_t central = 30 + name_len + size, total = central + 46 + name_len + 22;
    zip = calloc(1, total);
    if (!zip) goto done;
    uint32_t crc = (uint32_t)crc32(crc32(0L, Z_NULL, 0), (const Bytef *)json, (uInt)size);
    write32(zip, 0x04034b50U); write16(zip + 4, 20); write32(zip + 14, crc);
    write32(zip + 18, (uint32_t)size); write32(zip + 22, (uint32_t)size);
    write16(zip + 26, (uint16_t)name_len);
    memcpy(zip + 30, filename, name_len); memcpy(zip + 30 + name_len, json, size);
    uint8_t *dir = zip + central;
    write32(dir, 0x02014b50U); write16(dir + 4, 20); write16(dir + 6, 20);
    write32(dir + 16, crc); write32(dir + 20, (uint32_t)size); write32(dir + 24, (uint32_t)size);
    write16(dir + 28, (uint16_t)name_len); memcpy(dir + 46, filename, name_len);
    uint8_t *end = dir + 46 + name_len;
    write32(end, 0x06054b50U); write16(end + 8, 1); write16(end + 10, 1);
    write32(end + 12, (uint32_t)(46 + name_len)); write32(end + 16, (uint32_t)central);
    *data = zip; *len = total; zip = NULL; rc = 0;
done:
    free(zip); free(json); cJSON_Delete(root);
    return rc;
}
