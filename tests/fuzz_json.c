#include "config.h"
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 1024 * 1024) return 0;
    vl_server_t servers[64];
    size_t count = 0;
    (void)cfg_parse_subscription((const char *)data, size, servers,
                                 64, &count);
    return 0;
}
