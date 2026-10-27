#define _POSIX_C_SOURCE 200809L
#include "../servoCommandSocket.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static volatile sig_atomic_t alive = 1;
static void stop_mock(int signum) { (void)signum; alive = 0; }

typedef struct { ServoHealth *health; } TestContext;

static int mock_apply(char key, void *ctx)
{
    TestContext *test = (TestContext *)ctx;
    int pulse = key == 'u' ? 500 : key == 'i' ? 1520 : key == 'o' ? 2500 : -1;
    if (pulse < 0) return -1;
    servo_health_on_command_ok(test->health, key, pulse);
    printf("MOCK_SERVO_KEY: %c => %d us (NO HARDWARE)\n", key, pulse);
    fflush(stdout);
    return 0;
}

int main(void)
{
    ServoHealth health;
    servo_health_init(&health);
    servo_health_on_ready(&health, 1520);  /* simulation only */
    ServoCommandSocket socket = {.fd = -1};
    socket.health = &health;
    TestContext context = {.health = &health};
    signal(SIGINT, stop_mock);
    signal(SIGTERM, stop_mock);
    if (servo_command_start(&socket) != 0) { perror("servo_command_start"); return 1; }
    puts("Servo health mock ready (no hardware; Ctrl+C to stop)");
    fflush(stdout);
    int result = 0;
    while (alive) {
        if (servo_command_poll(&socket, mock_apply, &context) < 0) {
            perror("servo_command_poll"); result = 1; break;
        }
    }
    servo_command_stop(&socket);
    return result;
}
