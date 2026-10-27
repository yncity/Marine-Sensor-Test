#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../dvlCommandSocket.h"

static volatile sig_atomic_t keep_running = 1;
static void stop_mock(int unused) { (void)unused; keep_running = 0; }

int main(void)
{
    DvlHealth health;
    dvl_health_init(&health);
    int64_t now = dvl_health_now_ns();
    dvl_health_on_connect(&health, now);
    dvl_health_on_bytes(&health, 23, now);
    dvl_health_on_line(&health, "{\"type\":\"velocity\"}", now, 1);

    DvlCommandSocket cmd = {.fd = -1};
    cmd.health = &health;
    if (dvl_command_start(&cmd) != 0) { perror("DVL IPC mock"); return 1; }

    struct sigaction sa = {0};
    sa.sa_handler = stop_mock;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    double hz = 2.0;
    int64_t interval_ns = 500000000LL;
    int64_t next_tick = now + interval_ns;
    puts("DVL health IPC mock ready (no real sensor, Ctrl+C to stop).");
    fflush(stdout);
    while (keep_running) {
        struct pollfd pfd = {.fd = cmd.fd, .events = POLLIN};
        int rc = poll(&pfd, 1, 100);
        if (rc > 0 && (pfd.revents & POLLIN)) {
            dvl_command_accept(&cmd, &hz, &interval_ns, &next_tick);
        } else if (rc < 0 && errno != EINTR) {
            perror("poll");
            break;
        }
    }
    dvl_command_stop(&cmd);
    return 0;
}
