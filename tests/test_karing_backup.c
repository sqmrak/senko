#include "karing_backup.h"
#include "karing_zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed;
static void check(int ok, const char *name) {
    if (!ok) { fprintf(stderr, "FAIL %s\n", name); failed = 1; }
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--export") == 0) {
        static store_t fixture;
        const char *node = "socks5://user:pass@proxy.example:1080#proxy";
        size_t sub = 0, count = 0, length = 0;
        uint8_t *bytes = NULL;
        store_init(&fixture);
        if (store_add_sub(&fixture, "feed", "https://feed.example/sub", &sub) != STORE_OK ||
            store_refresh_sub(&fixture, sub, node, strlen(node), &count) != STORE_OK ||
            karing_export(&fixture, &bytes, &length) != 0) return 2;
        FILE *file = fopen(argv[2], "wb");
        if (!file || fwrite(bytes, 1, length, file) != length || fclose(file) != 0) return 2;
        free(bytes);
        return 0;
    }
    if (argc == 3) {
        static store_t fixture;
        FILE *file = fopen(argv[1], "rb");
        if (!file || fseek(file, 0, SEEK_END) != 0) return 2;
        long size = ftell(file);
        if (size < 0 || size > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) return 2;
        unsigned char *bytes = malloc((size_t)size + 1);
        if (!bytes || fread(bytes, 1, (size_t)size, file) != (size_t)size) return 2;
        fclose(file);
        karing_import_result_t result;
        store_init(&fixture);
        int ok = karing_import(&fixture, bytes, (size_t)size, &result) == 0;
        free(bytes);
        return ok == (atoi(argv[2]) != 0) ? 0 : 1;
    }
    static store_t original, restored, before;
    const char *manual = "vless://11111111-1111-4111-8111-111111111111@manual.example:443?security=tls&type=ws&path=%2Fsocket#manual";
    const char *feed = "socks5://user:password@proxy.example:1080#proxy";
    size_t sub = 0, added = 0, len = 0;
    uint8_t *zip = NULL;
    karing_import_result_t counts;
    store_init(&original);
    check(store_add_manual(&original, manual, NULL) == STORE_OK, "manual fixture");
    check(store_add_sub(&original, "feed", "https://feed.example/sub", &sub) == STORE_OK, "subscription fixture");
    check(store_refresh_sub(&original, sub, feed, strlen(feed), &added) == STORE_OK && added == 1, "subscription server");
    check(karing_export(&original, &zip, &len) == 0 && zip && len > 22, "export archive");
    store_init(&restored);
    check(karing_import(&restored, zip, len, &counts) == 0, "import archive");
    check(counts.subscriptions == 1 && counts.servers == 2 && restored.n == 2, "group counts");
    check(restored.group[0] == STORE_GROUP_MANUAL && restored.group[1] == 0, "group ownership");
    check(!strcmp(restored.servers[0].host, "manual.example") &&
          restored.servers[0].net == VL_NET_WS && !strcmp(restored.servers[0].path, "/socket"), "manual server fields");
    check(!strcmp(restored.subs[0].url, "https://feed.example/sub") &&
          !strcmp(restored.servers[1].user, "user") && !strcmp(restored.servers[1].pass, "password"), "subscription fields");
    before = restored;
    check(karing_import(&restored, zip, len, &counts) != 0 &&
          !memcmp(&before, &restored, sizeof before), "reimport leaves existing entries intact");
    check(karing_import(&restored, zip, len - 1, &counts) != 0 &&
          !memcmp(&before, &restored, sizeof before), "truncated archive leaves store intact");
    zip[45] ^= 1;
    check(karing_import(&restored, zip, len, &counts) != 0 &&
          !memcmp(&before, &restored, sizeof before), "damaged archive leaves store intact");
    free(zip);
    snprintf(original.subs[0].header, sizeof original.subs[0].header, "Authorization: secret");
    zip = NULL; len = 0;
    check(karing_export(&original, &zip, &len) != 0 && !zip && !len, "subscription header is not discarded");
    original.subs[0].header[0] = '\0';
    original.servers[0].net = VL_NET_XHTTP;
    zip = NULL; len = 0;
    check(karing_export(&original, &zip, &len) != 0 && !zip && !len, "unsupported export is explicit");
    if (!failed) puts("all Karing backup checks passed");
    return failed;
}
