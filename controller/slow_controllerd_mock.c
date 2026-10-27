#define _POSIX_C_SOURCE 200809L
/* 4-D-1 delayed-response IPC mock. Use ONLY when controllerd is stopped.
 * Listens on ./controller.sock, waits 4 seconds per command, then acknowledges.
 * Does not contact Sonar, DVL, Servo, or systemd.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static const char path[] = "./controller.sock";
static void on_stop(int signo) { (void)signo; running = 0; }

int main(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = on_stop;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) { perror("socket"); return 1; }
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    /* Do not unlink an existing socket: user must stop controllerd first. */
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind (stop controllerd first)"); close(server); return 1;
    }
    if (listen(server, 8) < 0) {
        perror("listen"); close(server); unlink(path); return 1;
    }
    puts("SLOW MOCK ready: responses delayed by 4 seconds; Ctrl+C to stop.");
    fflush(stdout);
    while (running) {
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept"); break;
        }
        char cmd[256] = {0};
        size_t n = 0;
        while (n + 1 < sizeof(cmd)) {
            char c;
            ssize_t rc = recv(client, &c, 1, 0);
            if (rc != 1 || c == '\n') break;
            cmd[n++] = c;
        }
        printf("RECEIVED: %s (delaying response)\n", cmd);
        fflush(stdout);
        struct timespec pause = {.tv_sec = 4, .tv_nsec = 0};
        while (running && nanosleep(&pause, &pause) < 0 && errno == EINTR) {}
        char response[320];
        snprintf(response, sizeof(response), "SLOW_MOCK_ACK: %.254s\n", cmd);
#if defined(MSG_NOSIGNAL)
        (void)send(client, response, strlen(response), MSG_NOSIGNAL);
#else
        (void)send(client, response, strlen(response), 0);
#endif
        close(client);
    }
    close(server);
    unlink(path);
    return 0;
}
