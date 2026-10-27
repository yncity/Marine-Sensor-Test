#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>

#define DEFAULT_SOCKET "./controller.sock"
#define MAX_COMMAND 256

static volatile sig_atomic_t keep_running = 1;

static void stop_requested(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
}

static const char *dispatch(const char *command)
{
    if (strcmp(command, "ping") == 0)
        return "PONG: controllerd is responding\n";
    if (strcmp(command, "help") == 0)
        return "AVAILABLE: ping, help\nPENDING: status, q, sonar keys, h -x, servo keys\n";
    if (strcmp(command, "status") == 0)
        return "NOT_IMPLEMENTED: device health/status is not connected yet\n";
    if (strcmp(command, "q") == 0)
        return "NOT_IMPLEMENTED: global stop is not connected; no device was stopped\n";
    return "UNKNOWN_COMMAND: not connected to a device yet\n";
}

static void handle_client(int client)
{
    char command[MAX_COMMAND];
    size_t length = 0;

    struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
    (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    while (length + 1 < sizeof(command))
    {
        char c;
        ssize_t n = recv(client, &c, 1, 0);
        if (n <= 0 || c == '\n')
            break;
        command[length++] = c;
    }
    command[length] = '\0';
    if (length > 0 && command[length - 1] == '\r')
        command[length - 1] = '\0';

    const char *response = dispatch(command);
    size_t remaining = strlen(response);
    while (remaining > 0)
    {
        ssize_t n = send(client, response, remaining, 0);
        if (n <= 0)
            break;
        response += n;
        remaining -= (size_t)n;
    }
}

int main(int argc, char **argv)
{
    const char *path = argc == 2 ? argv[1] : DEFAULT_SOCKET;
    if (argc > 2)
    {
        fprintf(stderr, "Usage: %s [unix-socket-path]\n", argv[0]);
        return EXIT_FAILURE;
    }

    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(address.sun_path))
    {
        fprintf(stderr, "Socket path too long\n");
        return EXIT_FAILURE;
    }
    strcpy(address.sun_path, path);

    struct sigaction sa = {0};
    sa.sa_handler = stop_requested;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) != 0 ||
        sigaction(SIGTERM, &sa, NULL) != 0)
    {
        perror("sigaction");
        return EXIT_FAILURE;
    }
    signal(SIGPIPE, SIG_IGN);

    mode_t old_mask = umask(0077);
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0)
    {
        perror("socket");
        umask(old_mask);
        return EXIT_FAILURE;
    }
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind (socket may already exist)");
        close(server);
        umask(old_mask);
        return EXIT_FAILURE;
    }
    umask(old_mask);
    if (listen(server, 8) < 0)
    {
        perror("listen");
        close(server);
        unlink(path);
        return EXIT_FAILURE;
    }

    printf("controllerd listening: %s\n", path);
    fflush(stdout);
    while (keep_running)
    {
        int client = accept(server, NULL, NULL);
        if (client < 0)
        {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }
        handle_client(client);
        close(client);
    }

    close(server);
    unlink(path);
    puts("controllerd stopped");
    return EXIT_SUCCESS;
}
