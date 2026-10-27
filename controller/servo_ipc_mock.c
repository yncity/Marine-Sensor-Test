#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include "../servoCommandSocket.h"

static volatile sig_atomic_t running = 1;
static void stop_mock(int signum)
{
    (void)signum;
    running = 0;
}
static int apply_mock_key(char key, void *ignored)
{
    (void)ignored;
    printf("MOCK_RECEIVED: %c\n", key);
    fflush(stdout);
    return 0;
}
int main(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = stop_mock;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    ServoCommandSocket cmd = {.fd = -1};
    if (servo_command_start(&cmd) != 0)
    {
        perror("servo_command_start");
        return 1;
    }
    puts("Servo mock is listening (Ctrl+C to stop).");
    fflush(stdout);
    while (running)
    {
        if (servo_command_poll(&cmd, apply_mock_key, NULL) < 0)
        {
            perror("servo_command_poll");
            servo_command_stop(&cmd);
            return 1;
        }
    }
    servo_command_stop(&cmd);
    return 0;
}
