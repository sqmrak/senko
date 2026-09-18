#include "control.h"
#include "traffic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char line[64];
    size_t n = 0;
    uint64_t up, down;
    assert(ctl_build_stat(UINT64_MAX, UINT64_MAX, line, sizeof line, &n) == CTL_OK);
    assert(ctl_parse_stat(line, n, &up, &down) == CTL_OK);
    assert(up == UINT64_MAX && down == UINT64_MAX);
    assert(ctl_parse_stat("STAT 0 0\r\n", 10, &up, &down) == CTL_OK);
    assert(up == 0 && down == 0);
    const char *bad[] = {
        "", "STATE 1 2", "STAT 1", "STAT -1 2", "STAT +1 2", "STAT  1 2",
        "STAT 1  2", "STAT 1 2 extra", "STAT 1 2 ", "STAT 1 2\nSTAT 3 4",
        "STAT 18446744073709551616 0", "STAT 0 18446744073709551616"
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        up = down = 42;
        assert(ctl_parse_stat(bad[i], strlen(bad[i]), &up, &down) == CTL_ERR_PARSE);
        assert(up == 0 && down == 0);
    }
    assert(ctl_parse_stat("STAT 1\0 2", 9, &up, &down) == CTL_ERR_PARSE);
    assert(ctl_build_stat(UINT64_MAX, UINT64_MAX, line, 8, &n) == CTL_ERR_BUF);
    ctl_state_t state = CTL_STATE_IDLE;
    long uptime = -1;
    assert(ctl_parse_state("STATE connected 73\n", 19, &state, &uptime) == CTL_OK);
    assert(state == CTL_STATE_CONNECTED && uptime == 73);
    assert(ctl_parse_state("STATE connected 0\n", 18, &state, &uptime) == CTL_OK);
    assert(state == CTL_STATE_CONNECTED && uptime == 0);
    assert(ctl_parse_state("STATE idle\n", 11, &state, &uptime) == CTL_OK);
    assert(state == CTL_STATE_IDLE && uptime == 0);
    const char *mixed_state = "STAT 1 2\nSTATE connected 3\n";
    assert(ctl_parse_state(mixed_state, strlen(mixed_state),
                           &state, &uptime) == CTL_ERR_PARSE);
    traffic_counter_t counter = {0, 0};
    traffic_counter_update(&counter, UINT32_MAX - 5);
    traffic_counter_update(&counter, 7);
    assert(counter.bytes == (uint64_t)UINT32_MAX + 8);
    traffic_counter_update(&counter, 7);
    assert(counter.bytes == (uint64_t)UINT32_MAX + 8);
    memset(&counter, 0, sizeof counter);
    traffic_counter_update(&counter, 10);
    assert(counter.bytes == 10);
    puts("all traffic checks passed");
    return 0;
}
