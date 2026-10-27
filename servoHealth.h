#ifndef MARINE_SERVO_HEALTH_H
#define MARINE_SERVO_HEALTH_H

/* Stage 3-D-4: software-side PWM command status only.
 * No actuator angle/position feedback is available.
 * POSIX-independent, usable on macOS and Linux.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int pwm_initialized;
    int last_pulse_us;
    char last_command;
    unsigned int commands_ok;
    unsigned int commands_failed;
    int pwm_update_failed;
} ServoHealth;

static inline void servo_health_init(ServoHealth *h)
{
    memset(h, 0, sizeof(*h));
}

/* Call only after all init writes, including PWM enable=1, succeeded. */
static inline void servo_health_on_ready(ServoHealth *h, int neutral_us)
{
    h->pwm_initialized = 1;
    h->last_pulse_us = neutral_us;
    h->pwm_update_failed = 0;
}

static inline void servo_health_on_command_ok(ServoHealth *h, char key, int pulse_us)
{
    h->last_pulse_us = pulse_us;
    h->last_command = key;
    ++h->commands_ok;
    h->pwm_update_failed = 0;
}

/* Retain last SUCCESSFULLY requested pulse, not the failed target pulse. */
static inline void servo_health_on_command_fail(ServoHealth *h, char key)
{
    h->last_command = key;
    ++h->commands_failed;
    h->pwm_update_failed = 1;
}

static inline void servo_health_report(const ServoHealth *h, char *out, size_t cap)
{
    const char *state = !h->pwm_initialized ? "STARTING" :
                        h->pwm_update_failed ? "ERROR" : "READY";
    const char *error = h->pwm_update_failed ? "PWM_UPDATE_FAILED" : "NONE";
    char key[2] = {h->last_command, '\0'};
    (void)snprintf(out, cap,
        "SERVO STATE: %s\n"
        "ERROR: %s\n"
        "PWM_INIT: %s\n"
        "LAST_PULSE_US: %d\n"
        "LAST_COMMAND: %s\n"
        "COMMANDS_OK: %u\n"
        "COMMANDS_FAILED: %u\n"
        "POSITION_FEEDBACK: NOT_AVAILABLE\n",
        state, error, h->pwm_initialized ? "SUCCESS" : "NOT_CONFIRMED",
        h->last_pulse_us, h->last_command ? key : "INIT",
        h->commands_ok, h->commands_failed);
}
#endif
