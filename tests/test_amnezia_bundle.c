#include "amnezia_bundle.h"
#include "awg_config.h"

#include <stdio.h>
#include <string.h>

#include "amnezia_fixture.inc"

static int failed;

static void expect(int condition, const char *what) {
    if (condition) return;
    fprintf(stderr, "failed: %s\n", what);
    failed = 1;
}

static void extract_ok(const char *body, const char *what) {
    char conf[8192];
    char reason[160];
    size_t len = 0;
    amz_status_t r = amz_bundle_extract_conf(body, strlen(body), conf, sizeof conf,
                                             &len, reason, sizeof reason);
    if (r != AMZ_OK) {
        fprintf(stderr, "failed: %s (%d: %s)\n", what, (int)r, reason);
        failed = 1;
        return;
    }
    expect(len == strlen(conf), "extracted length matches");
    expect(conf[len - 1] == '\n', "extracted config ends with a newline");
    expect(strstr(conf, "[Interface]") != NULL, "extracted config has an interface");

    /* the point of the decoder is a profile the tunnel can actually use */
    awg_config_t cfg;
    char why[160];
    awg_cfg_status_t p = awg_config_parse(conf, len, &cfg, why, sizeof why);
    if (p != AWG_CFG_OK) {
        fprintf(stderr, "failed: %s parses as awg (%d: %s)\n", what, (int)p, why);
        failed = 1;
        return;
    }
    expect(strcmp(cfg.endpoint_host, "203.0.113.7") == 0, "endpoint host");
    expect(cfg.endpoint_port == 51820, "endpoint port");
    expect(cfg.address_count == 1 && cfg.dns_count == 2, "network fields");
    expect(cfg.has_preshared_key, "preshared key");
}

static void extract_fails(const char *body, amz_status_t want, const char *what) {
    char conf[1024];
    char reason[160];
    size_t len = 0;
    amz_status_t r = amz_bundle_extract_conf(body, strlen(body), conf, sizeof conf,
                                             &len, reason, sizeof reason);
    if (r == want) return;
    fprintf(stderr, "failed: %s wanted %d got %d (%s)\n", what, (int)want, (int)r, reason);
    failed = 1;
}

int main(void) {
    expect(amz_bundle_looks_like(k_vpn_link, strlen(k_vpn_link)), "vpn link classified");
    expect(amz_bundle_looks_like(k_plain_bundle, strlen(k_plain_bundle)),
           "plain bundle classified");
    expect(!amz_bundle_looks_like("vless://x@1.2.3.4:443#a", 22), "vless is not a bundle");
    expect(!amz_bundle_looks_like("{\"outbounds\":[]}", 16), "xray json is not a bundle");
    expect(!amz_bundle_looks_like("", 0), "empty is not a bundle");

    extract_ok(k_vpn_link, "compressed vpn link");
    extract_ok(k_plain_bundle, "plain wireguard bundle");

    /* the share sheet hands over the link with a newline and a byte order mark */
    char padded[8192];
    snprintf(padded, sizeof padded, "\xEF\xBB\xBF  %s \n", k_vpn_link);
    extract_ok(padded, "vpn link with bom and whitespace");

    extract_fails("[Interface]\nPrivateKey = x\n", AMZ_ERR_NOT_BUNDLE, "native conf");
    extract_fails("vpn://????", AMZ_ERR_DECODE, "vpn link that is not base64");
    extract_fails("{\"containers\":[{\"container\":\"amnezia-openvpn\","
                  "\"openvpn\":{\"last_config\":\"{}\"}}]}",
                  AMZ_ERR_UNSUPPORTED, "bundle without a wireguard container");
    extract_fails("{\"containers\":[{\"container\":\"amnezia-awg\",\"awg\":{}}]}",
                  AMZ_ERR_JSON, "container without a config");
    extract_fails("{\"last_config\":\"", AMZ_ERR_JSON, "truncated json");

    /* a valid bundle must still refuse to overflow a caller buffer */
    {
        char small[32];
        char reason[160];
        size_t len = 0;
        amz_status_t r = amz_bundle_extract_conf(k_vpn_link, strlen(k_vpn_link),
                                                 small, sizeof small, &len,
                                                 reason, sizeof reason);
        expect(r == AMZ_ERR_SPACE && len == 0, "small output buffer");
    }

    if (failed) return 1;
    puts("all amnezia bundle checks passed");
    return 0;
}
