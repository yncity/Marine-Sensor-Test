#include "../servoHealth.h"
#include <stdio.h>
#include <string.h>

static int expect(const ServoHealth *h, const char *fragment)
{
    char output[512];
    servo_health_report(h, output, sizeof(output));
    if (!strstr(output, fragment)) {
        fprintf(stderr, "FAIL expected '%s' in:\n%s\n", fragment, output);
        return 0;
    }
    return 1;
}

int main(void)
{
    ServoHealth h;
    servo_health_init(&h);
    if (!expect(&h, "SERVO STATE: STARTING")) return 1;
    servo_health_on_ready(&h, 1520);
    if (!expect(&h, "SERVO STATE: READY") ||
        !expect(&h, "LAST_PULSE_US: 1520")) return 1;
    servo_health_on_command_ok(&h, 'u', 500);
    if (!expect(&h, "LAST_PULSE_US: 500") ||
        !expect(&h, "COMMANDS_OK: 1")) return 1;
    servo_health_on_command_fail(&h, 'o');
    if (!expect(&h, "SERVO STATE: ERROR") ||
        !expect(&h, "ERROR: PWM_UPDATE_FAILED") ||
        !expect(&h, "LAST_PULSE_US: 500")) return 1;
    servo_health_on_command_ok(&h, 'i', 1520);
    if (!expect(&h, "SERVO STATE: READY") ||
        !expect(&h, "COMMANDS_FAILED: 1") ||
        !expect(&h, "COMMANDS_OK: 2")) return 1;
    puts("SERVO_HEALTH_MOCK_PASS");
    return 0;
}
