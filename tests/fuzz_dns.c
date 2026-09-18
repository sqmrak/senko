#include "dns_msg.h"

#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    dns_question_t question;
    dns_response_info_t info;
    uint8_t output[2048];
    size_t output_len;

    if (dns_msg_parse_question(data, size, &question) == DNS_MSG_OK) {
        (void)dns_msg_build_block(data, size, DNS_BLOCK_ZERO,
                                  output, sizeof output, &output_len);
        (void)dns_msg_build_block(data, size, DNS_BLOCK_NXDOMAIN,
                                  output, sizeof output, &output_len);
        (void)dns_msg_build_block(data, size, DNS_BLOCK_REFUSED,
                                  output, sizeof output, &output_len);
    }
    (void)dns_msg_parse_response_question(data, size, &question);
    (void)dns_msg_response_info(data, size, &info);
    if (size <= sizeof output) {
        memcpy(output, data, size);
        (void)dns_msg_patch_ttls(output, size, 60);
        (void)dns_msg_clamp_ttls(output, size, 30, 3600);
    }
    return 0;
}
