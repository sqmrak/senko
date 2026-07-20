/* seal/open roundtrip for aes-128-gcm records; bad tag must reject*/
#include "../daemon/core/tls13_record.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ok(const char *name, int cond) {
    if (cond) printf("  ok  %s\n", name);
    else { printf(" FAIL %s\n", name); fails++; }
}

int main(void) {
    uint8_t key[16], iv[12];
    uint8_t plain[] = "application data!!";
    uint8_t wire[256], out[256];
    size_t wire_len = 0, out_len = 0;
    uint8_t out_type = 0;

    memset(key, 0x7e, sizeof key);
    memset(iv, 0x01, sizeof iv);

    ok("seal",
       tls13_record_seal(key, iv, 0, TLS13_CT_APPLICATION,
                         plain, sizeof plain - 1,
                         wire, sizeof wire, &wire_len) == TLS13_REC_OK);
    ok("wire has header", wire_len > 5 && wire[0] == 0x17);

    ok("open",
       tls13_record_open(key, iv, 0, wire, wire_len,
                         out, sizeof out, &out_len, &out_type) == TLS13_REC_OK);
    ok("type", out_type == TLS13_CT_APPLICATION);
    ok("payload",
       out_len == sizeof plain - 1 &&
       memcmp(out, plain, out_len) == 0);

    wire[wire_len - 1] ^= 0x01;
    ok("bad tag",
       tls13_record_open(key, iv, 0, wire, wire_len,
                         out, sizeof out, &out_len, &out_type) != TLS13_REC_OK);

    if (fails) {
        printf("%d tls13_record checks failed\n", fails);
        return 1;
    }
    printf("all tls13_record checks passed\n");
    return 0;
}
