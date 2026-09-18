#include "trojan_client.h"

#include <string.h>
#include <openssl/evp.h>

int trojan_hash_password(const char *password, char hex_out[TROJAN_HEX_LEN + 1]) {
    if (!password || !hex_out) return -1;
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return -1;
    if (EVP_DigestInit_ex(ctx, EVP_sha224(), NULL) != 1 ||
        EVP_DigestUpdate(ctx, password, strlen(password)) != 1 ||
        EVP_DigestFinal_ex(ctx, md, &md_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return -1;
    }
    EVP_MD_CTX_free(ctx);
    if (md_len != 28) return -1;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 28; ++i) {
        hex_out[i * 2]     = hex[(md[i] >> 4) & 0xf];
        hex_out[i * 2 + 1] = hex[md[i] & 0xf];
    }
    hex_out[TROJAN_HEX_LEN] = '\0';
    return 0;
}

void trojan_client_init(trojan_client_t *c, const char *password, const vless_dest_t *dest) {
    if (!c) return;
    memset(c, 0, sizeof *c);
    if (password) {
        size_t pl = strlen(password);
        if (pl >= sizeof c->password) pl = sizeof c->password - 1;
        memcpy(c->password, password, pl);
        c->password[pl] = '\0';
        trojan_hash_password(c->password, c->hex_hash);
    }
    if (dest) c->dest = *dest;
    c->initialized = 1;
}

int trojan_client_build_request(const trojan_client_t *c,
                                const uint8_t *payload, size_t payload_len,
                                uint8_t *out, size_t cap, size_t *out_len) {
    if (!c || !out || !out_len) return TR_ERR_ARG;
    if (!c->hex_hash[0]) return TR_ERR_PROTO;

    size_t len = 0;
    if (len + TROJAN_HEX_LEN + 2 > cap) return TR_ERR_ARG;
    memcpy(out + len, c->hex_hash, TROJAN_HEX_LEN);
    len += TROJAN_HEX_LEN;
    out[len++] = '\r';
    out[len++] = '\n';

    if (len + 1 > cap) return TR_ERR_ARG;
    out[len++] = 0x01; /* CONNECT */

    uint8_t atyp = 0;
    if (c->dest.atyp == VLESS_ADDR_IPV4) atyp = 0x01;
    else if (c->dest.atyp == VLESS_ADDR_DOMAIN) atyp = 0x03;
    else if (c->dest.atyp == VLESS_ADDR_IPV6) atyp = 0x04;
    else return TR_ERR_PROTO;

    if (len + 1 > cap) return TR_ERR_ARG;
    out[len++] = atyp;

    if (atyp == 0x01) {
        if (len + 4 > cap) return TR_ERR_ARG;
        memcpy(out + len, c->dest.host_addr, 4);
        len += 4;
    } else if (atyp == 0x03) {
        size_t dl = strlen(c->dest.domain);
        if (dl > 255 || len + 1 + dl > cap) return TR_ERR_ARG;
        out[len++] = (uint8_t)dl;
        memcpy(out + len, c->dest.domain, dl);
        len += dl;
    } else if (atyp == 0x04) {
        if (len + 16 > cap) return TR_ERR_ARG;
        memcpy(out + len, c->dest.host_addr, 16);
        len += 16;
    }

    if (len + 2 > cap) return TR_ERR_ARG;
    out[len++] = (uint8_t)(c->dest.port >> 8);
    out[len++] = (uint8_t)(c->dest.port & 0xff);

    if (len + 2 > cap) return TR_ERR_ARG;
    out[len++] = '\r';
    out[len++] = '\n';

    if (payload && payload_len > 0) {
        if (len + payload_len > cap) return TR_ERR_ARG;
        memcpy(out + len, payload, payload_len);
        len += payload_len;
    }

    *out_len = len;
    return TR_OK;
}
