#include "http.h"
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint8_t body[32768];
    http_parser_t parser;
    http_parser_init(&parser, body, sizeof body);
    size_t off = 0;
    while (off < size) {
        size_t chunk = data[off] % 97 + 1;
        if (chunk > size - off) chunk = size - off;
        if (http_parser_feed(&parser, data + off, chunk) != HTTP_NEED_MORE) break;
        off += chunk;
    }
    (void)http_parser_eof(&parser);
    return 0;
}
