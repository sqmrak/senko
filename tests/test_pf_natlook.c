#include <stdint.h>
#include <stdio.h>
#include <string.h>

int pf_natlook_parse_state_line_for_test(const char *line, uint16_t redir_port,
                                         uint16_t client_port, char *host,
                                         size_t host_cap, uint16_t *port);

static int g_fail = 0;

static void ok(const char *what, int cond) {
    if (cond) return;
    g_fail++;
    fprintf(stderr, "FAIL %s\n", what);
}

int main(void) {
    char host[64];
    uint16_t port = 0;

    const char *out_style =
        "ALL tcp 192.168.0.181:49351 -> 127.0.0.1:34370 -> 172.66.147.243:443";
    memset(host, 0, sizeof host);
    ok("out style parses",
       pf_natlook_parse_state_line_for_test(out_style, 12080, 34370,
                                            host, sizeof host, &port) == 0);
    ok("out style host", strcmp(host, "172.66.147.243") == 0);
    ok("out style port", port == 443);

    const char *in_style =
        "ALL tcp 127.0.0.1:12080 <- 172.66.147.243:443 <- 127.0.0.1:34370";
    memset(host, 0, sizeof host);
    port = 0;
    ok("in style parses",
       pf_natlook_parse_state_line_for_test(in_style, 12080, 34370,
                                            host, sizeof host, &port) == 0);
    ok("in style host", strcmp(host, "172.66.147.243") == 0);
    ok("in style port", port == 443);

    const char *stale_http =
        "ALL tcp 127.0.0.1:12080 <- 2.16.56.51:80 <- 127.0.0.1:48412";
    ok("stale in style ignored",
       pf_natlook_parse_state_line_for_test(stale_http, 12080, 34370,
                                            host, sizeof host, &port) != 0);

    const char *wrong_tail =
        "ALL tcp 127.0.0.1:12080 <- 172.66.147.243:443 <- 127.0.0.1:34371";
    ok("wrong client port ignored",
       pf_natlook_parse_state_line_for_test(wrong_tail, 12080, 34370,
                                            host, sizeof host, &port) != 0);

    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("all pf_natlook checks passed\n");
    return 0;
}
