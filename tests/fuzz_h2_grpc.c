#include "h2_core.h"
#include "grpc_core.h"
#include "hpack.h"
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    senko_hpack_fields_t hf;
    char hs[64];
    size_t hs_used = 0;
    (void)senko_hpack_parse(data, size, &hf);
    (void)senko_hpack_string(data, size, hs, sizeof hs, &hs_used);

    size_t off = 0;
    while (off < size) {
        senko_h2_frame_t frame;
        size_t used = 0;
        int r = senko_h2_parse_frame(data + off, size - off, &frame, &used);
        if (r <= 0) break;
        if (frame.type == 0 && frame.length) {
            uint8_t out[16384];
            size_t out_len = 0;
            (void)senko_grpc_decode(frame.payload, frame.length,
                                    out, sizeof out, &out_len);
        }
        off += used;
    }
    return 0;
}
