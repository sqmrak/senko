#define _DEFAULT_SOURCE

#include "senko_time.h"

#include <time.h>
#include <sys/time.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#include <pthread.h>

/* the kernel does not promise a reduced timebase fraction, and the unreduced
   form (1000000000/24000000) overflows uint64 the moment it is multiplied by
   a tick count. reducing it first keeps the whole conversion in integers, so
   the armv7 slice needs no double to int64 helper for a clock read */
static uint64_t gcd_u64(uint64_t a, uint64_t b) {
    while (b) { uint64_t t = a % b; a = b; b = t; }
    return a;
}

static uint64_t tick_num, tick_den;

static void tick_scale_init(void) {
    mach_timebase_info_data_t scale;
    if (mach_timebase_info(&scale) != KERN_SUCCESS || scale.denom == 0) return;
    uint64_t num = scale.numer;
    uint64_t den = (uint64_t)scale.denom * 1000000ull; /* fold ns -> ms in */
    uint64_t g = gcd_u64(num, den);
    tick_num = num / g;
    tick_den = den / g;
}
#endif

int64_t senko_now_ms(void) {
#ifdef __APPLE__
/* mach_absolute_time counts uptime, so it survives an ntp correction. every
   datagram of a tunnel reads this clock, so the reduced fraction is made once
   and not with a gcd loop on each read */
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, tick_scale_init);
    if (tick_den) {
        uint64_t t = mach_absolute_time();
        uint64_t ms = (t / tick_den) * tick_num + ((t % tick_den) * tick_num) / tick_den;
        return (int64_t)ms;
    }
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
/* last resort only: the wall clock can still step, but int64 at least keeps
   the multiplication exact where time_t is 32 bit */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}
