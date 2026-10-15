#include "senko_clock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static time_t wall = 1791860000;
static int64_t monotonic = 100000;
time_t time(time_t *out) { if (out) *out = wall; return wall; }
int64_t senko_now_ms(void) { return monotonic; }

int main(void) {
    int64_t t = -1, offset = -1;
    const char *date = "Sun, 06 Nov 1994 08:49:37 GMT";
    assert(senko_clock_parse_http_date(date, strlen(date), &t) == 0 && t == 784111777);
    const char *bad[] = {
        "Sun, 06 Nov 1994 08:49:37 GMTgarbage", "Mon, 06 Nov 1994 08:49:37 GMT",
        "Sun, 31 Feb 1994 08:49:37 GMT", "Sun, 06 Nov 1994 24:49:37 GMT",
        "Sun, 06 Nov 1994 08:60:37 GMT", "Sun, 06 Nov 1994 08:49:60 GMT",
        "Sun, 06 Nov 1994 08:49:37 UTC", "Sun, 29 Feb 2100 08:49:37 GMT", ""
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i)
        assert(senko_clock_parse_http_date(bad[i], strlen(bad[i]), &t) < 0);
    assert(senko_clock_parse_http_date("Tue, 29 Feb 2000 00:00:00 GMT", 29, &t) == 0);
    const char *head = "HTTP/1.1 204 No Content\r\ndAtE:\tSun, 06 Nov 1994 08:49:37 GMT \r\n\r\n";
    assert(senko_clock_from_http_head(head, strlen(head), &t) == 0 && t == 784111777);
    for (size_t n = 0; n < strlen(head); ++n)
        assert(senko_clock_from_http_head(head, n, &t) < 0);
    const char *body = "HTTP/1.1 200 OK\r\n\r\nDate: Sun, 06 Nov 1994 08:49:37 GMT\r\n";
    assert(senko_clock_from_http_head(body, strlen(body), &t) < 0);
    const char *dup = "HTTP/1.1 204 No Content\r\nDate: Sun, 06 Nov 1994 08:49:37 GMT\r\nDate: Sun, 06 Nov 1994 08:49:37 GMT\r\n\r\n";
    assert(senko_clock_from_http_head(dup, strlen(dup), &t) < 0);
    senko_clock_reset();
    assert(!senko_clock_offset(&offset) && offset == 0);
    senko_clock_note(wall - 3600);
    assert(senko_clock_offset(&offset) && offset == 3600);
    wall += 30; monotonic += 30000;
    assert(senko_clock_offset(&offset) && offset == 3600);
    wall -= 3600;
    assert(!senko_clock_offset(&offset) && offset == 0);
    senko_clock_note(wall - 3600);
    wall += 21601; monotonic += 21601000;
    assert(!senko_clock_offset(&offset));
    puts("clock parsing, freshness and correction checks passed");
    return 0;
}
