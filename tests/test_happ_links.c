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

    /* a panel that answers with its own page still carries the nodes in the
       markup, and the whole-line parser never saw them */
    const char *page =
        "<!DOCTYPE html>\n<html><body>\n"
        "<a class=\"btn\" href=\"vless://11111111-2222-3333-4444-555555555555"
        "@page.example:8443?security=tls&sni=page.example&type=tcp"
        "#page-one\">import</a>\n"
        "<p>see <a href=\"https://fonts.example/css?family=Nunito\">fonts</a></p>\n"
        "</body></html>\n";
    count = 0;
    ok("a node inside markup is imported",
       cfg_parse_subscription(page, strlen(page), servers, 8, &count) == CFG_OK &&
       count == 1 && strcmp(servers[0].host, "page.example") == 0);

    /* a page link is a page link: it must not become an https proxy */
    const char *plain_page =
        "<!DOCTYPE html><html><body>"
        "<link href=\"https://fonts.example/css2?family=Nunito\">"
        "<a href=\"https://apps.example/app/id123\">get it</a>"
        "</body></html>";
    count = 0;
    cfg_parse_subscription(plain_page, strlen(plain_page), servers, 8, &count);
    ok("page links are not turned into proxies", count == 0);
    ok("a page with no feed says so",
       cfg_reject_reason(plain_page, strlen(plain_page)) != NULL);

    /* the one shape senko genuinely cannot read has to name itself */
    const char *crypt5_page =
        "<!DOCTYPE html><html><body>"
        "<a href=\"happ://crypt5/fzvdSYbx2G3lRSCU2HlXiw58761w1zJ\">add</a>"
        "</body></html>";
    count = 0;
    cfg_parse_subscription(crypt5_page, strlen(crypt5_page), servers, 8, &count);
    ok("a crypt5 only page yields no node", count == 0);
    {
        const char *why = cfg_reject_reason(crypt5_page, strlen(crypt5_page));
        ok("a crypt5 only page is named as such",
           why != NULL && strstr(why, "crypt5") != NULL);
    }
    ok("a real feed has nothing to explain",
       cfg_reject_reason(nodes, strlen(nodes)) == NULL);

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
