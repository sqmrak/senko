#include "senko_clock.h"

#include <pthread.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "senko_time.h"

/* an observation older than this says little about a clock ntp may have fixed */
#define SENKO_CLOCK_FRESH_MS (6LL * 3600 * 1000)

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_known;
static int64_t g_offset_s;
static int64_t g_seen_ms;
static int64_t g_local_s;

void senko_clock_note(int64_t server_unix) {
    int64_t local = (int64_t)time(NULL);
    if (local < 0 || server_unix < 0 || server_unix > 253402300799LL) return;
    pthread_mutex_lock(&g_lock);
    g_known = 1;
    g_offset_s = local - server_unix;
    g_local_s = local;
    g_seen_ms = senko_now_ms();
    pthread_mutex_unlock(&g_lock);
}

int senko_clock_offset(int64_t *offset_s) {
    int known;
    pthread_mutex_lock(&g_lock);
    int64_t age = senko_now_ms() - g_seen_ms;
    int64_t elapsed = (int64_t)time(NULL) - g_local_s;
    /* a manual or ntp correction invalidates the old offset */
    known = g_known && age >= 0 && age <= SENKO_CLOCK_FRESH_MS &&
            elapsed >= age / 1000 - 2 && elapsed <= age / 1000 + 2;
    if (offset_s) *offset_s = 0;
    if (known && offset_s) *offset_s = g_offset_s;
    pthread_mutex_unlock(&g_lock);
    return known;
}

void senko_clock_reset(void) {
    pthread_mutex_lock(&g_lock);
    g_known = 0;
    g_offset_s = 0;
    g_seen_ms = 0;
    pthread_mutex_unlock(&g_lock);
}

/* days since 1970-01-01 of a proleptic gregorian date, without timegm, which
   old ios libc does not promise */
static int64_t days_from_civil(int64_t y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int digits(const char *p, int n, int *out) {
    int v = 0;
    for (int i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9') return -1;
        v = v * 10 + (p[i] - '0');
    }
    *out = v;
    return 0;
}

int senko_clock_parse_http_date(const char *text, size_t len, int64_t *unix_out) {
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    /* "Sun, 06 Nov 1994 08:49:37 GMT" is exactly 29 characters */
    if (!text || !unix_out || len != 29) return -1;
    const char *p = text;
    if (p[3] != ',' || p[4] != ' ' || p[7] != ' ' || p[11] != ' ' || p[16] != ' ' ||
        p[19] != ':' || p[22] != ':' || p[25] != ' ' || strncmp(p + 26, "GMT", 3) != 0)
        return -1;
    static const char weekdays[] = "SunMonTueWedThuFriSat";
    int weekday = -1;
    for (int i = 0; i < 7; ++i)
        if (memcmp(p, weekdays + i * 3, 3) == 0) weekday = i;
    if (weekday < 0) return -1;
    int day, year, hour, minute, second, month = 0;
    if (digits(p + 5, 2, &day) || digits(p + 12, 4, &year) || digits(p + 17, 2, &hour) ||
        digits(p + 20, 2, &minute) || digits(p + 23, 2, &second))
        return -1;
    for (int i = 0; i < 12; ++i)
        if (strncmp(p + 8, months + 3 * i, 3) == 0) month = i + 1;
    if (!month || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59 ||
        year < 1970)
        return -1;
    static const int month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int max_day = month_days[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ++max_day;
    if (day > max_day) return -1;
    int64_t days = days_from_civil(year, month, day);
    if ((days + 4) % 7 != weekday) return -1;
    *unix_out = days * 86400 + hour * 3600 + minute * 60 + second;
    return 0;
}

int senko_clock_from_http_head(const char *head, size_t len, int64_t *unix_out) {
    if (!head || !unix_out) return -1;
    int found = 0;
    int64_t value = 0;
    size_t i = 0;
    while (i + 1 < len && !(head[i] == '\r' && head[i + 1] == '\n')) ++i;
    if (i + 1 >= len) return -1;
    i += 2;
    while (i + 1 < len) {
        if (head[i] == '\r' && head[i + 1] == '\n') {
            if (!found) return -1;
            *unix_out = value;
            return 0;
        }
        size_t end = i;
        while (end + 1 < len && !(head[end] == '\r' && head[end + 1] == '\n')) ++end;
        if (end + 1 >= len) return -1;
        if (end - i >= 5 && strncasecmp(head + i, "Date:", 5) == 0) {
            if (found) return -1;
            size_t v = i + 5, tail = end;
            while (v < tail && (head[v] == ' ' || head[v] == '\t')) ++v;
            while (tail > v && (head[tail - 1] == ' ' || head[tail - 1] == '\t')) --tail;
            if (senko_clock_parse_http_date(head + v, tail - v, &value) != 0) return -1;
            found = 1;
        }
        i = end + 2;
    }
    return -1;
}
