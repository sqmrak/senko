#ifndef SENKO_UPLOAD_H
#define SENKO_UPLOAD_H

#include "session.h"

void senko_upload_in_feed(session_t *s, const uint8_t *buf, size_t len);
void senko_upload_wire_feed(session_t *s, const uint8_t *buf, size_t len);
void senko_upload_flush(session_t *s);

#endif