#define SENKO_HOST_TEST

#include "subfetch.h"
#include "http.h"

#include <stdio.h>
#include <string.h>

static int failed;

static char got[256];
static size_t got_len;

static int collect_sink(void *ctx, const uint8_t *data, size_t len) {
    (void)ctx;
    if (got_len + len > sizeof got) return -1;
    memcpy(got + got_len, data, len);
    got_len += len;
    return 0;
}

static int failing_sink(void *ctx, const uint8_t *data, size_t len) {
    (void)ctx; (void)data; (void)len;
    return -1;
}

static void expect(const char *what, unsigned long long got, unsigned long long want) {
    if (got == want) return;
    fprintf(stderr, "FAIL %s: got %llu want %llu\n", what, got, want);
    failed++;
}

int main(void) {
    /* the last field carries no trailing ';', which is where the parser used to
       lose its final digit and report every subscription as expired */
    const char *tail =
        "upload=100; download=200; total=500; expire=1735689600";
    expect("expire is last field",
           subfetch_userinfo_expire_for_test(tail), 1735689600ULL);
    expect("total before last field",
           subfetch_userinfo_value_for_test(tail, "total"), 500ULL);

    const char *middle =
        "expire=1735689600; upload=1; download=2; total=3";
    expect("expire in the middle",
           subfetch_userinfo_expire_for_test(middle), 1735689600ULL);
    expect("total is last field",
           subfetch_userinfo_value_for_test(middle, "total"), 3ULL);

    expect("single field",
           subfetch_userinfo_expire_for_test("expire=1735689600"), 1735689600ULL);
    expect("trailing semicolon",
           subfetch_userinfo_expire_for_test("expire=1735689600;"), 1735689600ULL);
    /* a padded last field kept its spaces inside the number and was dropped */
    expect("padded key and value",
           subfetch_userinfo_expire_for_test("upload=1;  expire = 1735689600 "),
           1735689600ULL);
    expect("padded value of a counter",
           subfetch_userinfo_value_for_test("total = 500 ; upload=1", "total"), 500ULL);
    /* panels that answer in milliseconds or in days left used to land in 1970,
       and the list then called a live subscription expired */
    expect("milliseconds",
           subfetch_userinfo_expire_for_test("expire=1735689600000"), 1735689600ULL);
    expect("privio expiry beyond 2100",
           subfetch_userinfo_expire_for_test("expire=4898316803"), 4898316803ULL);
    expect("far future milliseconds",
           subfetch_userinfo_expire_for_test("expire=4898316803000"), 4898316803ULL);
    expect("days left is not a timestamp",
           subfetch_userinfo_expire_for_test("expire=30"), 0ULL);
    expect("second after the epoch is not a timestamp",
           subfetch_userinfo_expire_for_test("expire=42"), 0ULL);
    expect("expired subscription is still reported",
           subfetch_userinfo_expire_for_test("expire=1600000000"), 1600000000ULL);
    expect("missing key",
           subfetch_userinfo_expire_for_test("upload=1; total=2"), 0ULL);
    expect("empty value",
           subfetch_userinfo_expire_for_test("expire="), 0ULL);
    expect("non numeric", subfetch_userinfo_expire_for_test("expire=abc"), 0ULL);
    expect("null header", subfetch_userinfo_expire_for_test(NULL), 0ULL);
    expect("null wanted",
           subfetch_userinfo_value_for_test("total=5", NULL), 0ULL);

/* the exact headers a remnawave panel answered with while it refused this
   device: an http 200 whose body is a single placeholder node. taking that body
   as the node list is what made every row in the list carry one name */
    {
        static const char gated[] =
            "HTTP/1.1 200 OK\r\n"
            "content-type: text/plain; charset=utf-8\r\n"
            "content-length: 12\r\n"
            "announce: base64:0J/RgNC10LLRi9GI0LXQvSDQu9C40LzQuNGCINGD0YHRgtGA0L7QudGB0YLQsi4=\r\n"
            "subscription-userinfo: upload=0; download=10923885868; "
            "total=1073741824000; expire=1790132922\r\n"
            "x-hwid-active: true\r\n"
            "x-hwid-limit: true\r\n"
            "x-hwid-max-devices-reached: true\r\n"
            "\r\n"
            "placeholder\n";
        uint8_t body[256];
        http_parser_t hp;
        subfetch_info_t info;
        http_parser_init(&hp, body, sizeof body);
        expect("gated response parses",
               http_parser_feed(&hp, (const uint8_t *)gated, sizeof gated - 1) == HTTP_DONE,
               1);
        memset(&info, 0, sizeof info);
        subfetch_parser_info_for_test(&hp, &info);
        expect("device gating is detected", info.gated ? 1 : 0, 1);
        expect("gate reason is decoded",
               strcmp(info.gate_reason,
                      "\xD0\x9F\xD1\x80\xD0\xB5\xD0\xB2\xD1\x8B\xD1\x88\xD0\xB5"
                      "\xD0\xBD \xD0\xBB\xD0\xB8\xD0\xBC\xD0\xB8\xD1\x82 "
                      "\xD1\x83\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xB9\xD1\x81"
                      "\xD1\x82\xD0\xB2.") == 0 ? 1 : 0, 1);
        expect("userinfo still read on a gated reply", info.expire, 1790132922ULL);
    }

    {
        static const char plain[] =
            "HTTP/1.1 200 OK\r\n"
            "content-length: 5\r\n"
            "subscription-userinfo: upload=0; download=1; total=2; expire=1790132922\r\n"
            "\r\n"
            "hello";
        uint8_t body[64];
        http_parser_t hp;
        subfetch_info_t info;
        http_parser_init(&hp, body, sizeof body);
        expect("plain response parses",
               http_parser_feed(&hp, (const uint8_t *)plain, sizeof plain - 1) == HTTP_DONE,
               1);
        memset(&info, 0, sizeof info);
        subfetch_parser_info_for_test(&hp, &info);
        expect("no gating without the header", info.gated ? 1 : 0, 0);
        expect("no gate reason without the header", info.gate_reason[0] ? 1 : 0, 0);
    }

/* profile-title used to be folded into the same field as subscription-description,
   so the panel's suggested name for the subscription was stranded on a details
   screen and never became the subscription's actual name */
    {
        static const char titled[] =
            "HTTP/1.1 200 OK\r\n"
            "content-length: 5\r\n"
            "profile-title: My Plan\r\n"
            "subscription-description: a longer blurb about the plan\r\n"
            "\r\n"
            "hello";
        uint8_t body[64];
        http_parser_t hp;
        subfetch_info_t info;
        http_parser_init(&hp, body, sizeof body);
        expect("titled response parses",
               http_parser_feed(&hp, (const uint8_t *)titled, sizeof titled - 1) == HTTP_DONE,
               1);
        memset(&info, 0, sizeof info);
        subfetch_parser_info_for_test(&hp, &info);
        expect("profile-title becomes the title, not the description",
               strcmp(info.title, "My Plan") == 0 ? 1 : 0, 1);
        expect("subscription-description stays a separate field",
               strcmp(info.description, "a longer blurb about the plan") == 0 ? 1 : 0, 1);
    }

/* a happ panel delivers its routing profile in a header of several kilobytes; one
   past the buffer is dropped, because a cut base64 profile decodes to a wrong one */
    {
        static char response[40 * 1024];
        static char value[HTTP_ROUTING_MAX];
        uint8_t body[16];
        http_parser_t hp;
        subfetch_info_t info;
        int n;
        memset(value, 'A', sizeof value - 1);
        value[sizeof value - 1] = '\0';
        n = snprintf(response, sizeof response,
                     "HTTP/1.1 200 OK\r\ncontent-length: 2\r\nRouting: happ://routing/onadd/%s\r\n\r\nok",
                     value);
        http_parser_init(&hp, body, sizeof body);
        expect("oversized routing header still parses",
               http_parser_feed(&hp, (const uint8_t *)response, (size_t)n) == HTTP_DONE, 1);
        memset(&info, 0, sizeof info);
        subfetch_parser_info_for_test(&hp, &info);
        expect("oversized routing header is dropped, not cut", info.routing[0] ? 1 : 0, 0);
        expect("oversized routing header is flagged", info.routing_too_long, 1);

        n = snprintf(response, sizeof response,
                     "HTTP/1.1 200 OK\r\ncontent-length: 2\r\nRouting: happ://routing/onadd/e30=\r\n\r\nok");
        http_parser_init(&hp, body, sizeof body);
        expect("routing response parses",
               http_parser_feed(&hp, (const uint8_t *)response, (size_t)n) == HTTP_DONE, 1);
        memset(&info, 0, sizeof info);
        subfetch_parser_info_for_test(&hp, &info);
        expect("routing header is kept",
               strcmp(info.routing, "happ://routing/onadd/e30=") == 0 ? 1 : 0, 1);
        expect("routing header was not too long", info.routing_too_long, 0);

        memset(value, 'B', 12 * 1024);
        value[12 * 1024] = '\0';
        n = snprintf(response, sizeof response,
                     "HTTP/1.1 200 OK\r\ncontent-length: 2\r\nrouting: %s\r\n\r\nok", value);
        http_parser_init(&hp, body, sizeof body);
        expect("a 12 KB routing header parses",
               http_parser_feed(&hp, (const uint8_t *)response, (size_t)n) == HTTP_DONE, 1);
        memset(&info, 0, sizeof info);
        subfetch_parser_info_for_test(&hp, &info);
        expect("a 12 KB routing header is kept whole", strlen(info.routing) == 12 * 1024 ? 1 : 0, 1);
    }

/* a large download goes to a sink in pieces, in either framing, and the limit on
   its total is enforced before the bytes that pass it are written */
    {
        static const char fixed[] =
            "HTTP/1.1 200 OK\r\ncontent-length: 10\r\n\r\n0123456789";
        static const char chunked[] =
            "HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\n"
            "4\r\nabcd\r\n3\r\nefg\r\n0\r\n\r\n";
        static const char redirect[] =
            "HTTP/1.1 302 Found\r\nlocation: https://x.test/y\r\ncontent-length: 5\r\n\r\nhello";
        http_parser_t hp;
        size_t i;
        got_len = 0;
        http_parser_init(&hp, NULL, 64);
        http_parser_set_sink(&hp, collect_sink, NULL);
        for (i = 0; i < sizeof fixed - 1; i += 7) {
            size_t n = sizeof fixed - 1 - i < 7 ? sizeof fixed - 1 - i : 7;
            http_parser_feed(&hp, (const uint8_t *)fixed + i, n);
        }
        expect("sink gets a fixed length body", got_len == 10 && memcmp(got, "0123456789", 10) == 0, 1);
        expect("sink counts the body", hp.body_len, 10);

        got_len = 0;
        http_parser_init(&hp, NULL, 64);
        http_parser_set_sink(&hp, collect_sink, NULL);
        expect("sink chunked parse", http_parser_feed(&hp, (const uint8_t *)chunked, sizeof chunked - 1) == HTTP_DONE, 1);
        expect("sink gets a chunked body", got_len == 7 && memcmp(got, "abcdefg", 7) == 0, 1);

        got_len = 0;
        http_parser_init(&hp, NULL, 9);
        http_parser_set_sink(&hp, collect_sink, NULL);
        expect("sink limit refuses a longer body",
               http_parser_feed(&hp, (const uint8_t *)fixed, sizeof fixed - 1) == HTTP_ERR_TOOBIG, 1);
        expect("sink limit marks the cut", hp.body_cut, 1);
        expect("sink limit writes nothing past it", got_len <= 9, 1);

        got_len = 0;
        http_parser_init(&hp, NULL, 64);
        http_parser_set_sink(&hp, collect_sink, NULL);
        expect("a redirect body never reaches the sink",
               http_parser_feed(&hp, (const uint8_t *)redirect, sizeof redirect - 1) == HTTP_DONE && got_len == 0, 1);

        got_len = 0;
        http_parser_init(&hp, NULL, 64);
        http_parser_set_sink(&hp, failing_sink, NULL);
        expect("a failing sink stops the parse",
               http_parser_feed(&hp, (const uint8_t *)fixed, sizeof fixed - 1) == HTTP_ERR_PARSE, 1);
    }

    if (failed) {
        fprintf(stderr, "%d check(s) failed\n", failed);
        return 1;
    }
    puts("all subscription userinfo checks passed");
    return 0;
}
