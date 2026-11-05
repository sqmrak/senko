#include "karing_zip.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define KARING_ZIP_LIMIT (16U * 1024U * 1024U)
#define KARING_JSON_LIMIT (4U * 1024U * 1024U)

static uint16_t read16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)read16(p) | ((uint32_t)read16(p + 2) << 16);
}

int karing_zip_subscriptions(const uint8_t *data, size_t len, char **json, size_t *json_len) {
    static const char name[] = "karing_subscribe.json";
    size_t end, start, pos, dir_end;
    uint32_t dir_offset, dir_size;
    uint16_t count;
    int found = 0;
    char *result = NULL;
    size_t result_len = 0;

    if (json) *json = NULL;
    if (json_len) *json_len = 0;
    if (!data || !json || !json_len || len < 22 || len > KARING_ZIP_LIMIT) return -1;
    start = len > 65557U ? len - 65557U : 0;
    end = len - 22;
    for (;;) {
        if (read32(data + end) == 0x06054b50U &&
            end + 22U + read16(data + end + 20) == len) break;
        if (end == start) return -1;
        --end;
    }
    count = read16(data + end + 10);
    dir_size = read32(data + end + 12);
    dir_offset = read32(data + end + 16);
    if (read16(data + end + 4) || read16(data + end + 6) ||
        read16(data + end + 8) != count || count == 0xffffU ||
        dir_offset > end || dir_size > end - dir_offset) return -1;
    pos = dir_offset;
    dir_end = pos + dir_size;
    for (unsigned i = 0; i < count; ++i) {
        uint16_t flags, method, name_len, extra_len, comment_len;
        uint32_t compressed, uncompressed, offset, crc;
        size_t next, data_offset;
        if (pos > dir_end || dir_end - pos < 46 || read32(data + pos) != 0x02014b50U) goto fail;
        flags = read16(data + pos + 8);
        method = read16(data + pos + 10);
        crc = read32(data + pos + 16);
        compressed = read32(data + pos + 20);
        uncompressed = read32(data + pos + 24);
        name_len = read16(data + pos + 28);
        extra_len = read16(data + pos + 30);
        comment_len = read16(data + pos + 32);
        offset = read32(data + pos + 42);
        next = pos + 46U + name_len + extra_len + comment_len;
        if (next > dir_end || read16(data + pos + 34)) goto fail;
        if (name_len == sizeof name - 1 && !memcmp(data + pos + 46, name, sizeof name - 1)) {
            z_stream stream;
            if (found++ || (flags & 1U) || (method != 0 && method != 8) ||
                compressed > KARING_ZIP_LIMIT || uncompressed > KARING_JSON_LIMIT ||
                offset > dir_offset || dir_offset - offset < 30 ||
                read32(data + offset) != 0x04034b50U || read16(data + offset + 8) != method ||
                read16(data + offset + 6) != flags || read16(data + offset + 26) != name_len) goto fail;
            data_offset = (size_t)offset + 30U + name_len + read16(data + offset + 28);
            if (data_offset > dir_offset || compressed > dir_offset - data_offset ||
                memcmp(data + offset + 30, name, name_len)) goto fail;
            result = malloc((size_t)uncompressed + 1);
            if (!result) goto fail;
            if (method == 0) {
                if (compressed != uncompressed) goto fail;
                memcpy(result, data + data_offset, uncompressed);
            } else {
                memset(&stream, 0, sizeof stream);
                stream.next_in = (Bytef *)(data + data_offset);
                stream.avail_in = compressed;
                stream.next_out = (Bytef *)result;
                stream.avail_out = uncompressed ? uncompressed : 1;
                if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) goto fail;
                int rc = inflate(&stream, Z_FINISH);
                inflateEnd(&stream);
                if (rc != Z_STREAM_END || stream.total_out != uncompressed ||
                    stream.total_in != compressed) goto fail;
            }
            if ((uint32_t)crc32(crc32(0L, Z_NULL, 0), (const Bytef *)result, uncompressed) != crc) goto fail;
            result[uncompressed] = '\0';
            result_len = uncompressed;
        }
        pos = next;
    }
    if (!found || pos != dir_end) goto fail;
    *json = result;
    *json_len = result_len;
    return 0;
fail:
    free(result);
    return -1;
}
