/* old ios lacks clock_gettime, so provide the millisecond hook with gettimeofday */
#include "mbedtls/build_info.h"
#include "mbedtls/platform_util.h"
#include <sys/time.h>
mbedtls_ms_time_t mbedtls_ms_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (mbedtls_ms_time_t)tv.tv_sec * 1000 + (mbedtls_ms_time_t)(tv.tv_usec / 1000);
}
