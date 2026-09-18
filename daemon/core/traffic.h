#ifndef SENKO_TRAFFIC_H
#define SENKO_TRAFFIC_H

#include <stdint.h>

typedef struct {
    uint32_t previous;
    uint64_t bytes;
} traffic_counter_t;

/* darwin if_data wraps at 4 GiB; sample before another full turn */
static inline void traffic_counter_update(traffic_counter_t *counter, uint32_t value) {
    counter->bytes += (uint32_t)(value - counter->previous);
    counter->previous = value;
}

#endif
