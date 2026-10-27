#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include "../dvlHealth.h"

static int check(const DvlHealth *h, int64_t time_ns, const char *state,
                 const char *error)
{
    char text[400];
    dvl_health_report_at(h, time_ns, text, sizeof(text));
    char expected_state[80], expected_error[80];
    snprintf(expected_state, sizeof(expected_state), "DVL STATE: %s\n", state);
    snprintf(expected_error, sizeof(expected_error), "ERROR: %s\n", error);
    if (!strstr(text, expected_state) || !strstr(text, expected_error)) {
        fprintf(stderr, "FAIL at %.1f seconds:\n%s", time_ns / 1e9, text);
        return 1;
    }
    printf("PASS %-10s (%s)\n", state, error);
    return 0;
}

int main(void)
{
    DvlHealth h;
    dvl_health_init(&h);
    int fail = 0;
    const int64_t sec = 1000000000LL;

    fail |= check(&h, 1*sec, "STARTING", "NONE");
    dvl_health_on_connect(&h, 2*sec);
    fail |= check(&h, 3*sec, "CONNECTED", "NONE");
    dvl_health_on_bytes(&h, 24, 4*sec);
    dvl_health_on_line(&h, "{\"type\":\"velocity\"}", 4*sec, 1);
    fail |= check(&h, 5*sec, "RUNNING", "NONE");
    dvl_health_on_bytes(&h, 18, 15*sec);
    dvl_health_on_line(&h, "not a JSON object", 15*sec, 0);
    fail |= check(&h, 16*sec, "DEGRADED", "NO_COMPLETE_JSON_FRAME");
    dvl_health_on_oversized(&h);
    fail |= check(&h, 27*sec, "ERROR", "DATA_TIMEOUT");
    if (h.frames_received != 1 || h.velocity_frames != 1 ||
        h.malformed_lines != 1 || h.oversized_lines != 1) fail = 1;
    puts(fail ? "DVL_HEALTH_MOCK_FAIL" : "DVL_HEALTH_MOCK_PASS");
    return fail;
}
