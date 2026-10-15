#ifndef SENKO_CLOCK_H
#define SENKO_CLOCK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* how far this device's wall clock is from the servers', estimated from the
   Date header of http answers senko already receives. an ios device with a
   dead rtc battery boots years off, and vmess, reality and certificate checks
   then fail in ways that look like a broken server */

/* records one server time against the wall clock now */
void senko_clock_note(int64_t server_unix);

/* 1 and the device minus server offset in seconds when an answer within the
   last six hours set it and the device clock has not changed, 0 when nothing recent is known */
int senko_clock_offset(int64_t *offset_s);

/* an rfc 7231 IMF-fixdate, "Sun, 06 Nov 1994 08:49:37 GMT"; 0 on success */
int senko_clock_parse_http_date(const char *text, size_t len, int64_t *unix_out);

/* the Date header of an http response head; 0 on success */
int senko_clock_from_http_head(const char *head, size_t len, int64_t *unix_out);

/* forgets every observation, for tests */
void senko_clock_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* SENKO_CLOCK_H */
