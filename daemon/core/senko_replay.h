#ifndef SENKO_REPLAY_H
#define SENKO_REPLAY_H

#include "session.h"

void senko_replay_feed(session_t *s, const uint8_t *buf, size_t len);
void senko_replay_flush(session_t *s);

#endif