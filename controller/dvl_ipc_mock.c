#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <poll.h>
#include <signal.h>
#include "../dvlCommandSocket.h"
static volatile sig_atomic_t running = 1;
static void stop(int signum) {(void)signum; running = 0;}
int main(void) {
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    DvlCommandSocket cmd = {.fd = -1};
    if (dvl_command_start(&cmd) != 0) {perror("dvl_command_start"); return 1;}
    double hz = 2.0;
    int64_t interval_ns = 500000000LL;
    int64_t next_tick = dvl_command_now_ns() + interval_ns;
    while (running) {
        struct pollfd pfd = {.fd = cmd.fd, .events = POLLIN};
        int rc = poll(&pfd, 1, 100);
        if (rc > 0 && (pfd.revents & POLLIN))
            dvl_command_accept(&cmd, &hz, &interval_ns, &next_tick);
    }
    dvl_command_stop(&cmd);
    return 0;
}
