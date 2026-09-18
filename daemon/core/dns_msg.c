#include "dns_msg.h"

#include <ctype.h>
#include <string.h>

#define DNS_HEADER_LEN 12
#define DNS_POINTER_MASK 0xc0
#define DNS_MAX_JUMPS 32

static uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void write_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void write_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static int skip_name(const uint8_t *msg, size_t len, size_t *offset) {
    size_t pos;
    unsigned labels = 0;
    if (!msg || !offset || *offset >= len) return -1;
    pos = *offset;
    while (pos < len) {
        uint8_t part = msg[pos++];
        if (part == 0) {
            *offset = pos;
            return 0;
        }
        if ((part & DNS_POINTER_MASK) == DNS_POINTER_MASK) {
            if (pos >= len) return -1;
            *offset = pos + 1;
            return 0;
        }
        if ((part & DNS_POINTER_MASK) != 0 || part > 63 ||
            part > len - pos || ++labels > 127)
            return -1;
        pos += part;
    }
    return -1;
}

static int decode_name(const uint8_t *msg, size_t len, size_t *offset,
                       char *out, size_t cap) {
    size_t pos;
    size_t consumed = 0;
    size_t out_len = 0;
    unsigned jumps = 0;
    int jumped = 0;
    if (!msg || !offset || !out || cap == 0 || *offset >= len) return -1;
    pos = *offset;
    while (pos < len) {
        uint8_t part = msg[pos++];
        if (part == 0) {
            if (!jumped) consumed = pos - *offset;
            if (out_len == 0 || out_len >= cap) return -1;
            out[out_len] = '\0';
            *offset += consumed;
            return 0;
        }
        if ((part & DNS_POINTER_MASK) == DNS_POINTER_MASK) {
            uint16_t pointer;
            if (pos >= len || ++jumps > DNS_MAX_JUMPS) return -1;
            pointer = (uint16_t)(((uint16_t)(part & 0x3f) << 8) | msg[pos++]);
            if (pointer >= len) return -1;
            if (!jumped) consumed = pos - *offset;
            pos = pointer;
            jumped = 1;
            continue;
        }
        if ((part & DNS_POINTER_MASK) != 0 || part > 63 || part > len - pos)
            return -1;
        if (out_len != 0) {
            if (out_len + 1 >= cap) return -1;
            out[out_len++] = '.';
        }
        if (out_len + part >= cap) return -1;
        for (uint8_t i = 0; i < part; ++i) {
            unsigned char c = msg[pos++];
            if (c <= 0x20 || c >= 0x7f) return -1;
            out[out_len++] = (char)tolower(c);
        }
        if (!jumped) consumed = pos - *offset;
    }
    return -1;
}

static int skip_question(const uint8_t *msg, size_t len, size_t *offset) {
    if (skip_name(msg, len, offset) != 0 || len - *offset < 4) return -1;
    *offset += 4;
    return 0;
}

static int rr_bounds(const uint8_t *msg, size_t len, size_t *offset,
                     uint16_t *type, uint16_t *class_code, uint32_t *ttl,
                     size_t *rdata, uint16_t *rdlength, size_t *ttl_offset) {
    size_t pos = *offset;
    if (skip_name(msg, len, &pos) != 0 || len - pos < 10) return -1;
    if (type) *type = read_u16(msg + pos);
    if (class_code) *class_code = read_u16(msg + pos + 2);
    if (ttl) *ttl = read_u32(msg + pos + 4);
    if (ttl_offset) *ttl_offset = pos + 4;
    uint16_t size = read_u16(msg + pos + 8);
    pos += 10;
    if (size > len - pos) return -1;
    if (rdata) *rdata = pos;
    if (rdlength) *rdlength = size;
    *offset = pos + size;
    return 0;
}

static dns_msg_status_t parse_question(const uint8_t *msg, size_t len,
                                       int response, dns_question_t *out) {
    size_t pos = DNS_HEADER_LEN;
    if (out) memset(out, 0, sizeof *out);
    if (!msg || !out || len < DNS_HEADER_LEN) return DNS_MSG_ERR_ARG;
    if (((read_u16(msg + 2) & 0x8000u) != 0) != response ||
        read_u16(msg + 4) != 1)
        return DNS_MSG_ERR_FORMAT;
    if (decode_name(msg, len, &pos, out->name, sizeof out->name) != 0 ||
        len - pos < 4)
        return DNS_MSG_ERR_FORMAT;
    out->type = read_u16(msg + pos);
    out->class_code = read_u16(msg + pos + 2);
    out->question_end = pos + 4;
    return DNS_MSG_OK;
}

dns_msg_status_t dns_msg_parse_question(const uint8_t *msg, size_t len,
                                        dns_question_t *out) {
    return parse_question(msg, len, 0, out);
}

dns_msg_status_t dns_msg_parse_response_question(const uint8_t *msg, size_t len,
                                                 dns_question_t *out) {
    return parse_question(msg, len, 1, out);
}

static int find_opt(const uint8_t *msg, size_t len, size_t *opt_start,
                    size_t *opt_len) {
    size_t pos = DNS_HEADER_LEN;
    uint16_t counts[4];
    for (size_t i = 0; i < 4; ++i) counts[i] = read_u16(msg + 4 + i * 2);
    for (uint16_t i = 0; i < counts[0]; ++i)
        if (skip_question(msg, len, &pos) != 0) return -1;
    for (size_t section = 1; section < 4; ++section) {
        for (uint16_t i = 0; i < counts[section]; ++i) {
            size_t start = pos;
            uint16_t type;
            if (rr_bounds(msg, len, &pos, &type, NULL, NULL, NULL, NULL, NULL) != 0)
                return -1;
            if (section == 3 && type == 41 && *opt_len == 0) {
                *opt_start = start;
                *opt_len = pos - start;
            }
        }
    }
    return pos == len ? 0 : -1;
}

static int append_bytes(uint8_t *out, size_t cap, size_t *pos,
                        const void *src, size_t len) {
    if (len > cap - *pos) return -1;
    memcpy(out + *pos, src, len);
    *pos += len;
    return 0;
}

dns_msg_status_t dns_msg_build_block(const uint8_t *query, size_t query_len,
                                     dns_block_response_t mode,
                                     uint8_t *out, size_t cap, size_t *out_len) {
    dns_question_t question;
    size_t opt_start = 0;
    size_t opt_len = 0;
    size_t pos;
    uint16_t answer_count = 0;
    uint16_t authority_count = 0;
    uint16_t rcode = 0;
    if (out_len) *out_len = 0;
    if (!query || !out || !out_len || cap < DNS_HEADER_LEN ||
        mode < DNS_BLOCK_ZERO || mode > DNS_BLOCK_REFUSED)
        return DNS_MSG_ERR_ARG;
    if (dns_msg_parse_question(query, query_len, &question) != DNS_MSG_OK ||
        find_opt(query, query_len, &opt_start, &opt_len) != 0)
        return DNS_MSG_ERR_FORMAT;

    if (mode == DNS_BLOCK_ZERO && question.class_code == 1 &&
        (question.type == 1 || question.type == 28))
        answer_count = 1;
    else if (mode == DNS_BLOCK_NXDOMAIN) {
        authority_count = 1;
        rcode = 3;
    } else if (mode == DNS_BLOCK_REFUSED) {
        rcode = 5;
    }

    memset(out, 0, DNS_HEADER_LEN);
    memcpy(out, query, 2);
    uint16_t query_flags = read_u16(query + 2);
    write_u16(out + 2, (uint16_t)(0x8080u | (query_flags & 0x7910u) | rcode));
    write_u16(out + 4, 1);
    write_u16(out + 6, answer_count);
    write_u16(out + 8, authority_count);
    write_u16(out + 10, opt_len ? 1 : 0);
    pos = DNS_HEADER_LEN;
    if (append_bytes(out, cap, &pos, query + DNS_HEADER_LEN,
                     question.question_end - DNS_HEADER_LEN) != 0)
        return DNS_MSG_ERR_SPACE;

    if (answer_count) {
        uint8_t rr[28];
        size_t rr_len = question.type == 1 ? 16 : 28;
        memset(rr, 0, sizeof rr);
        rr[0] = 0xc0;
        rr[1] = 0x0c;
        write_u16(rr + 2, question.type);
        write_u16(rr + 4, 1);
        write_u32(rr + 6, 60);
        write_u16(rr + 10, question.type == 1 ? 4 : 16);
        if (append_bytes(out, cap, &pos, rr, rr_len) != 0)
            return DNS_MSG_ERR_SPACE;
    }

    if (authority_count) {
        uint8_t soa[34];
        memset(soa, 0, sizeof soa);
        soa[0] = 0xc0;
        soa[1] = 0x0c;
        write_u16(soa + 2, 6);
        write_u16(soa + 4, 1);
        write_u32(soa + 6, 60);
        write_u16(soa + 10, 22);
        write_u32(soa + 14, 0);
        write_u32(soa + 18, 60);
        write_u32(soa + 22, 60);
        write_u32(soa + 26, 60);
        write_u32(soa + 30, 60);
        if (append_bytes(out, cap, &pos, soa, sizeof soa) != 0)
            return DNS_MSG_ERR_SPACE;
    }

    if (opt_len && append_bytes(out, cap, &pos, query + opt_start, opt_len) != 0)
        return DNS_MSG_ERR_SPACE;
    *out_len = pos;
    return DNS_MSG_OK;
}

static int skip_rdata_name(const uint8_t *msg, size_t len, size_t *pos,
                           size_t end) {
    size_t next = *pos;
    if (skip_name(msg, len, &next) != 0 || next > end) return -1;
    *pos = next;
    return 0;
}

dns_msg_status_t dns_msg_response_info(const uint8_t *msg, size_t len,
                                       dns_response_info_t *out) {
    size_t pos = DNS_HEADER_LEN;
    uint16_t qd, an, ns, ar;
    uint16_t rcode;
    uint32_t negative_ttl = 0;
    int have_answer_ttl = 0;
    if (out) memset(out, 0, sizeof *out);
    if (!msg || !out || len < DNS_HEADER_LEN) return DNS_MSG_ERR_ARG;
    if ((read_u16(msg + 2) & 0x8000u) == 0) return DNS_MSG_ERR_FORMAT;
    qd = read_u16(msg + 4);
    an = read_u16(msg + 6);
    ns = read_u16(msg + 8);
    ar = read_u16(msg + 10);
    rcode = (uint16_t)(read_u16(msg + 2) & 0x000fu);
    for (uint16_t i = 0; i < qd; ++i)
        if (skip_question(msg, len, &pos) != 0) return DNS_MSG_ERR_FORMAT;

    for (size_t section = 0; section < 3; ++section) {
        uint16_t count = section == 0 ? an : section == 1 ? ns : ar;
        for (uint16_t i = 0; i < count; ++i) {
            uint16_t type, class_code, rdlength;
            uint32_t ttl;
            size_t rdata;
            if (rr_bounds(msg, len, &pos, &type, &class_code, &ttl,
                          &rdata, &rdlength, NULL) != 0)
                return DNS_MSG_ERR_FORMAT;
            if (section == 0) {
                if (!have_answer_ttl || ttl < out->min_ttl) out->min_ttl = ttl;
                have_answer_ttl = 1;
                if (type == 1 && class_code == 1 && rdlength == 4 &&
                    out->ipv4_count < DNS_MSG_MAX_IPV4) {
                    out->ipv4[out->ipv4_count++] = read_u32(msg + rdata);
                }
            } else if (section == 1 && type == 6 && rdlength >= 22) {
                size_t soa_pos = rdata;
                size_t soa_end = rdata + rdlength;
                if (skip_rdata_name(msg, len, &soa_pos, soa_end) != 0 ||
                    skip_rdata_name(msg, len, &soa_pos, soa_end) != 0 ||
                    soa_end - soa_pos < 20)
                    return DNS_MSG_ERR_FORMAT;
                uint32_t minimum = read_u32(msg + soa_end - 4);
                negative_ttl = ttl < minimum ? ttl : minimum;
            }
        }
    }
    if (pos != len) return DNS_MSG_ERR_FORMAT;
    out->negative = an == 0 && (rcode == 0 || rcode == 3);
    if (out->negative) out->min_ttl = negative_ttl;
    return DNS_MSG_OK;
}

dns_msg_status_t dns_msg_patch_ttls(uint8_t *msg, size_t len,
                                    uint32_t elapsed_seconds) {
    size_t pos = DNS_HEADER_LEN;
    uint16_t counts[4];
    if (!msg || len < DNS_HEADER_LEN) return DNS_MSG_ERR_ARG;
    for (size_t i = 0; i < 4; ++i) counts[i] = read_u16(msg + 4 + i * 2);
    for (uint16_t i = 0; i < counts[0]; ++i)
        if (skip_question(msg, len, &pos) != 0) return DNS_MSG_ERR_FORMAT;
    for (size_t section = 1; section < 4; ++section) {
        for (uint16_t i = 0; i < counts[section]; ++i) {
            uint16_t type;
            uint32_t ttl;
            size_t ttl_offset;
            if (rr_bounds(msg, len, &pos, &type, NULL, &ttl, NULL, NULL,
                          &ttl_offset) != 0)
                return DNS_MSG_ERR_FORMAT;
            if (type != 41)
                write_u32(msg + ttl_offset,
                          elapsed_seconds >= ttl ? 0 : ttl - elapsed_seconds);
        }
    }
    return pos == len ? DNS_MSG_OK : DNS_MSG_ERR_FORMAT;
}

dns_msg_status_t dns_msg_clamp_ttls(uint8_t *msg, size_t len,
                                    uint32_t minimum, uint32_t maximum) {
    size_t pos = DNS_HEADER_LEN;
    uint16_t counts[4];
    if (!msg || len < DNS_HEADER_LEN || minimum > maximum)
        return DNS_MSG_ERR_ARG;
    for (size_t i = 0; i < 4; ++i) counts[i] = read_u16(msg + 4 + i * 2);
    for (uint16_t i = 0; i < counts[0]; ++i)
        if (skip_question(msg, len, &pos) != 0) return DNS_MSG_ERR_FORMAT;
    for (size_t section = 1; section < 4; ++section) {
        for (uint16_t i = 0; i < counts[section]; ++i) {
            uint16_t type, rdlength;
            uint32_t ttl;
            size_t rdata, ttl_offset;
            if (rr_bounds(msg, len, &pos, &type, NULL, &ttl, &rdata, &rdlength,
                          &ttl_offset) != 0)
                return DNS_MSG_ERR_FORMAT;
            if (type == 41) continue;
            if (ttl < minimum) ttl = minimum;
            if (ttl > maximum) ttl = maximum;
            write_u32(msg + ttl_offset, ttl);
            if (type == 6 && rdlength >= 22) {
                size_t soa_pos = rdata;
                size_t soa_end = rdata + rdlength;
                if (skip_rdata_name(msg, len, &soa_pos, soa_end) != 0 ||
                    skip_rdata_name(msg, len, &soa_pos, soa_end) != 0 ||
                    soa_end - soa_pos < 20)
                    return DNS_MSG_ERR_FORMAT;
                uint32_t value = read_u32(msg + soa_end - 4);
                if (value < minimum) value = minimum;
                if (value > maximum) value = maximum;
                write_u32(msg + soa_end - 4, value);
            }
        }
    }
    return pos == len ? DNS_MSG_OK : DNS_MSG_ERR_FORMAT;
}
