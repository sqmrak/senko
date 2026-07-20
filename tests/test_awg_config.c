#include "awg_config.h"

#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *what) {
    if (condition) return 0;
    fprintf(stderr, "failed: %s\n", what);
    return 1;
}

int main(void) {
    static const char sample[] =
        "[Interface]\n"
        "PrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "Address = 10.0.0.2/32, fd00::2/128\n"
        "DNS = 1.1.1.1, 2606:4700:4700::1111\n"
        "Jc = 4\nJmin = 32\nJmax = 96\n"
        "S1 = 18\nS2 = 19\nS3 = 20\nS4 = 21\n"
        "H1 = 100-200\nH2 = 201\nH3 = 202\nH4 = 203\n"
        "I1 = <b 0x0102><r 3><t>\n"
        "I2 =\nI3 = \nI4 =\nI5 =\n"
        "MTU = 1280\n"
        "[Peer]\n"
        "PublicKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "PresharedKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "AllowedIPs = 0.0.0.0/0, ::/0\n"
        "Endpoint = [2606:4700::1]:51820\n"
        "PersistentKeepalive = 25\n";
    awg_config_t cfg;
    char reason[128];
    awg_cfg_status_t r = awg_config_parse(sample, sizeof sample - 1, &cfg, reason, sizeof reason);
    int bad = 0;
    bad |= expect(r == AWG_CFG_OK, "sample parse");
    bad |= expect(cfg.address_count == 2 && cfg.dns_count == 2, "csv fields");
    bad |= expect(strcmp(cfg.endpoint_host, "2606:4700::1") == 0 && cfg.endpoint_port == 51820,
                  "ipv6 endpoint");
    bad |= expect(cfg.header_min[0] == 100 && cfg.header_max[0] == 200, "header range");
    bad |= expect(cfg.has_preshared_key && cfg.padding[3] == 21, "awg fields");
    bad |= expect(cfg.signature[1][0] == '\0' && cfg.signature[4][0] == '\0',
                  "empty optional signatures");

    static const char bad_range[] =
        "[Interface]\nPrivateKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "Jmin = 10\nJmax = 9\n"
        "[Peer]\nPublicKey = AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\n"
        "Endpoint = 127.0.0.1:51820\n";
    r = awg_config_parse(bad_range, sizeof bad_range - 1, &cfg, reason, sizeof reason);
    bad |= expect(r == AWG_CFG_ERR_RANGE, "junk range rejection");

    r = awg_config_load_file("../../warp.conf", &cfg, reason, sizeof reason);
    if (r == AWG_CFG_OK) {
        bad |= expect(strcmp(cfg.endpoint_host, "162.159.192.1") == 0 && cfg.endpoint_port == 500,
                      "warp endpoint");
        bad |= expect(cfg.jc == 120 && cfg.jmin == 23 && cfg.jmax == 911, "warp obfuscation");
        bad |= expect(cfg.address_count == 2 && cfg.dns_count == 4, "warp network fields");
        bad |= expect(strlen(cfg.signature[0]) > 2000, "warp signature");
    } else {
        /* optional external fixture - not shipped in the source tree */
        fprintf(stderr, "skip: warp.conf not present (%s)\n", reason);
    }
    return bad ? 1 : 0;
}
