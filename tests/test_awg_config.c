#include "awg_config.h"

#include <stdio.h>
#include <string.h>

/* real 32 byte keys: an all-zero key is what a missing key line leaves behind
   and the parser now refuses it, so the fixtures cannot use one */
#define K_PRIV "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="
#define K_PUB  "ICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj8="
#define K_PSK  "QEFCQ0RFRkdISUpLTE1OT1BRUlNUVVZXWFlaW1xdXl8="

static int expect(int condition, const char *what) {
    if (condition) return 0;
    fprintf(stderr, "failed: %s\n", what);
    return 1;
}

static awg_cfg_status_t parse(const char *text, awg_config_t *cfg,
                              char *reason, size_t reason_cap) {
    return awg_config_parse(text, strlen(text), cfg, reason, reason_cap);
}

static int expect_status(const char *text, awg_cfg_status_t want, const char *what) {
    awg_config_t cfg;
    char reason[160];
    awg_cfg_status_t r = parse(text, &cfg, reason, sizeof reason);
    if (r == want) return 0;
    fprintf(stderr, "failed: %s wanted %d got %d (%s)\n", what, (int)want, (int)r, reason);
    return 1;
}

int main(void) {
    static const char sample[] =
        "[Interface]\n"
        "PrivateKey = " K_PRIV "\n"
        "Address = 10.0.0.2/32, fd00::2/128\n"
        "DNS = 1.1.1.1, 2606:4700:4700::1111\n"
        "Jc = 4\nJmin = 32\nJmax = 96\n"
        "S1 = 18\nS2 = 19\nS3 = 20\nS4 = 21\n"
        "H1 = 100-200\nH2 = 201\nH3 = 202\nH4 = 203\n"
        "I1 = <b 0x0102><r 3><t>\n"
        "I2 =\nI3 = \nI4 =\nI5 =\n"
        "MTU = 1280\n"
        "[Peer]\n"
        "PublicKey = " K_PUB "\n"
        "PresharedKey = " K_PSK "\n"
        "AllowedIPs = 0.0.0.0/0, ::/0\n"
        "Endpoint = [2606:4700::1]:51820\n"
        "PersistentKeepalive = 25\n";
    awg_config_t cfg;
    char reason[160];
    awg_cfg_status_t r = parse(sample, &cfg, reason, sizeof reason);
    int bad = 0;
    bad |= expect(r == AWG_CFG_OK, "sample parse");
    bad |= expect(cfg.address_count == 2 && cfg.dns_count == 2, "csv fields");
    bad |= expect(strcmp(cfg.endpoint_host, "2606:4700::1") == 0 && cfg.endpoint_port == 51820,
                  "ipv6 endpoint");
    bad |= expect(cfg.header_min[0] == 100 && cfg.header_max[0] == 200, "header range");
    bad |= expect(cfg.has_preshared_key && cfg.padding[3] == 21, "awg fields");
    bad |= expect(cfg.signature[1][0] == '\0' && cfg.signature[4][0] == '\0',
                  "empty optional signatures");
    bad |= expect(cfg.itime == 0 && cfg.controlled[0][0] == '\0',
                  "awg 1.5 fields default to unset");

    /* amneziawg 1.5 adds the controlled junk packets and the junk train timer */
    static const char awg15[] =
        "[Interface]\n"
        "PrivateKey = " K_PRIV "\n"
        "Address = 10.0.0.2/32\n"
        "Jc = 400\nJmin = 32\nJmax = 96\n"
        "I1 = <b 0xc00000><rc 8>\n"
        "J1 = <b 0x1701><rd 4>\nJ2 = <r 12>\nJ3 = <t>\n"
        "Itime = 120\n"
        "[Peer]\n"
        "PublicKey = " K_PUB "\n"
        "Endpoint = vpn.example.org:51820\n"
        "PersistentKeepalive = off\n";
    r = parse(awg15, &cfg, reason, sizeof reason);
    bad |= expect(r == AWG_CFG_OK, "awg 1.5 parse");
    bad |= expect(cfg.jc == 400, "junk count above the old 128 cap");
    bad |= expect(cfg.itime == 120, "itime");
    bad |= expect(strcmp(cfg.controlled[0], "<b 0x1701><rd 4>") == 0 &&
                  strcmp(cfg.controlled[2], "<t>") == 0, "controlled junk packets");
    bad |= expect(cfg.persistent_keepalive == 0, "keepalive off");

    /* an exporter that writes a key with no value must not lose the profile */
    static const char blanks[] =
        "[Interface]\n"
        "PrivateKey = " K_PRIV "\n"
        "Address = 10.0.0.2/32\n"
        "DNS =\n"
        "MTU =\n"
        "[Peer]\n"
        "PublicKey = " K_PUB "\n"
        "PresharedKey =\n"
        "Endpoint = 198.51.100.4:51820\n";
    r = parse(blanks, &cfg, reason, sizeof reason);
    bad |= expect(r == AWG_CFG_OK, "blank optional values");
    bad |= expect(!cfg.has_preshared_key && cfg.dns_count == 0, "blank values stay unset");
    bad |= expect(cfg.mtu == 1280, "blank mtu keeps the default");

    static const char bad_range[] =
        "[Interface]\nPrivateKey = " K_PRIV "\nAddress = 10.0.0.2/32\n"
        "Jmin = 10\nJmax = 9\n"
        "[Peer]\nPublicKey = " K_PUB "\nEndpoint = 127.0.0.1:51820\n";
    bad |= expect_status(bad_range, AWG_CFG_ERR_RANGE, "junk range rejection");

    static const char no_private[] =
        "[Interface]\nAddress = 10.0.0.2/32\n"
        "[Peer]\nPublicKey = " K_PUB "\nEndpoint = 127.0.0.1:51820\n";
    bad |= expect_status(no_private, AWG_CFG_ERR_MISSING, "missing private key");

    static const char no_address[] =
        "[Interface]\nPrivateKey = " K_PRIV "\n"
        "[Peer]\nPublicKey = " K_PUB "\nEndpoint = 127.0.0.1:51820\n";
    bad |= expect_status(no_address, AWG_CFG_ERR_MISSING, "missing address");

    static const char blank_required[] =
        "[Interface]\nPrivateKey =\nAddress = 10.0.0.2/32\n"
        "[Peer]\nPublicKey = " K_PUB "\nEndpoint = 127.0.0.1:51820\n";
    bad |= expect_status(blank_required, AWG_CFG_ERR_FORMAT, "blank private key");

    static const char two_peers[] =
        "[Interface]\nPrivateKey = " K_PRIV "\nAddress = 10.0.0.2/32\n"
        "[Peer]\nPublicKey = " K_PUB "\nEndpoint = 127.0.0.1:51820\n"
        "[Peer]\nPublicKey = " K_PSK "\nEndpoint = 127.0.0.2:51820\n";
    bad |= expect_status(two_peers, AWG_CFG_ERR_FORMAT, "second peer rejection");

    /* awg 2.0 wire options senko cannot produce have to fail loudly instead of
       building a tunnel that never completes a handshake */
    static const char header_protection_needs_padding[] =
        "[Interface]\nPrivateKey = " K_PRIV "\nAddress = 10.0.0.2/32\n"
        "HeaderProtectionKey = " K_PSK "\n"
        "[Peer]\nPublicKey = " K_PUB "\nEndpoint = 127.0.0.1:51820\n";
    bad |= expect_status(header_protection_needs_padding, AWG_CFG_ERR_RANGE,
                         "header protection without enough s-prefix junk");

    static const char header_protection[] =
        "[Interface]\nPrivateKey = " K_PRIV "\nAddress = 10.0.0.2/32\n"
        "S1 = 12\nS2 = 12\nS3 = 12\nS4 = 12\n"
        "HeaderProtectionKey = " K_PSK "\n"
        "[Peer]\nPublicKey = " K_PUB "\nEndpoint = 127.0.0.1:51820\n";
    r = parse(header_protection, &cfg, reason, sizeof reason);
    bad |= expect(r == AWG_CFG_OK, "header protection with 12 byte s-prefixes parses");
    bad |= expect(cfg.has_header_protection, "header protection flag set");

    static const char trailers_on[] =
        "[Interface]\nPrivateKey = " K_PRIV "\nAddress = 10.0.0.2/32\n"
        "RandomTrailers = on\n"
        "[Peer]\nPublicKey = " K_PUB "\nEndpoint = 127.0.0.1:51820\n";
    bad |= expect_status(trailers_on, AWG_CFG_ERR_UNSUPPORTED, "random trailers on");

    /* a wire option written with no value is unset, not turned on */
    static const char blank_wire_options[] =
        "[Interface]\nPrivateKey = " K_PRIV "\nAddress = 10.0.0.2/32\n"
        "HeaderProtectionKey =\nRandomTrailers =\nContentPaddingAddition =\n"
        "[Peer]\nPublicKey = " K_PUB "\nEndpoint = 127.0.0.1:51820\n";
    bad |= expect_status(blank_wire_options, AWG_CFG_OK, "blank awg 2.0 wire options");

    /* the same switches turned off describe the wire senko already speaks */
    static const char trailers_off[] =
        "[Interface]\nPrivateKey = " K_PRIV "\nAddress = 10.0.0.2/32\n"
        "RandomTrailers = off\nContentPaddingAddition = 0\nDisableCookies = on\n"
        "RekeyAfterTime = 120\nMaxHandshakeAttempts = 5\n"
        "[Peer]\nPublicKey = " K_PUB "\nEndpoint = 127.0.0.1:51820\n";
    bad |= expect_status(trailers_off, AWG_CFG_OK, "inert awg 2.0 knobs");

    /* real exporters randomize the keepalive the same way they randomize
       the junk and rekey timers, and a real profile with a huge split
       tunnel list must not be truncated into an unparsable peer value */
    static const char keepalive_range[] =
        "[Interface]\nPrivateKey = " K_PRIV "\nAddress = 10.0.0.2/32\n"
        "[Peer]\nPublicKey = " K_PUB "\nEndpoint = 127.0.0.1:51820\n"
        "PersistentKeepalive = 25-35\n";
    r = parse(keepalive_range, &cfg, reason, sizeof reason);
    bad |= expect(r == AWG_CFG_OK, "persistent keepalive range parses");
    bad |= expect(cfg.persistent_keepalive >= 25 && cfg.persistent_keepalive <= 35,
                  "persistent keepalive lands inside its range");

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
    if (!bad) puts("all awg config checks passed");
    return bad ? 1 : 0;
}
