/* happ deep links: the action word the app puts in front of the payload, and
   the subscription url most of those links actually carry */
#include "config.h"
#include "happ.h"

#include <stdio.h>
#include <string.h>

static int failed;

static void ok(const char *name, int cond) {
    if (!cond) {
        printf("FAIL %s\n", name);
        failed++;
    }
}

/* url-safe base64 without padding, which is what the share links use */
static void b64url(const char *text, char *out, size_t cap) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t n = strlen(text), i = 0, o = 0;
    while (i + 2 < n && o + 4 < cap) {
        unsigned v = ((unsigned char)text[i] << 16) |
                     ((unsigned char)text[i + 1] << 8) |
                     (unsigned char)text[i + 2];
        out[o++] = alphabet[(v >> 18) & 63];
        out[o++] = alphabet[(v >> 12) & 63];
        out[o++] = alphabet[(v >> 6) & 63];
        out[o++] = alphabet[v & 63];
        i += 3;
    }
    if (i < n && o + 4 < cap) {
        unsigned v = (unsigned char)text[i] << 16;
        int tail = 1;
        if (i + 1 < n) { v |= (unsigned char)text[i + 1] << 8; tail = 2; }
        out[o++] = alphabet[(v >> 18) & 63];
        out[o++] = alphabet[(v >> 12) & 63];
        if (tail == 2) out[o++] = alphabet[(v >> 6) & 63];
    }
    out[o] = '\0';
}

int main(void) {
    char payload[512];
    char link[768];
    char plain[8192];
    char url[512];
    vl_server_t servers[8];
    size_t count = 0;

    const char *sub = "https://panel.example/sub/abc123def456";
    b64url(sub, payload, sizeof payload);

    snprintf(link, sizeof link, "happ://add/%s", payload);
    ok("add/ unwraps to the subscription url",
       happ_unwrap(link, plain, sizeof plain) == 0 && strcmp(plain, sub) == 0);

    snprintf(link, sizeof link, "happ://install-config/%s", payload);
    ok("install-config/ unwraps the same way",
       happ_unwrap(link, plain, sizeof plain) == 0 && strcmp(plain, sub) == 0);

    snprintf(link, sizeof link, "happ://subscription/%s", payload);
    ok("subscription/ unwraps the same way",
       happ_unwrap(link, plain, sizeof plain) == 0 && strcmp(plain, sub) == 0);

    /* the bare form the older links use still has to work */
    snprintf(link, sizeof link, "happ://%s", payload);
    ok("payload without an action word still unwraps",
       happ_unwrap(link, plain, sizeof plain) == 0 && strcmp(plain, sub) == 0);

    snprintf(link, sizeof link, "happ://add/%s", payload);
    ok("the import path reports a subscription url",
       cfg_subscription_url(link, strlen(link), url, sizeof url) == 0 &&
       strcmp(url, sub) == 0);

    ok("a bare url is a subscription url too",
       cfg_subscription_url(sub, strlen(sub), url, sizeof url) == 0 &&
       strcmp(url, sub) == 0);

    /* a link with a port and credentials is a proxy senko can dial, so it must
       not be mistaken for a feed */
    const char *proxy = "https://user:pass@proxy.example:8443#node";
    ok("a dialable https proxy is not a subscription url",
       cfg_subscription_url(proxy, strlen(proxy), url, sizeof url) != 0);

    /* a happ link carrying real nodes still imports as nodes */
    const char *nodes =
        "vless://11111111-2222-3333-4444-555555555555@node.example:443"
        "?security=tls&sni=node.example&type=tcp#one";
    b64url(nodes, payload, sizeof payload);
    snprintf(link, sizeof link, "happ://add/%s", payload);
    ok("a happ link with nodes is not reported as a subscription",
       cfg_subscription_url(link, strlen(link), url, sizeof url) != 0);
    count = 0;
    ok("a happ link with nodes parses into servers",
       cfg_parse_subscription(link, strlen(link), servers, 8, &count) == CFG_OK &&
       count == 1 && strcmp(servers[0].host, "node.example") == 0);

    /* crypt5 still has no keytable, and a wrong answer is worse than none */
    ok("crypt5 is refused rather than guessed",
       happ_unwrap("happ://crypt5/AAAA", plain, sizeof plain) != 0);
    ok("crypt5 behind an action word is refused too",
       happ_unwrap("happ://add/crypt5/AAAA", plain, sizeof plain) != 0);

    if (failed) {
        printf("%d happ link check(s) failed\n", failed);
        return 1;
    }
    printf("all happ link checks passed\n");
    return 0;
}
