#ifndef DVL_COMMAND_SOCKET_H
#define DVL_COMMAND_SOCKET_H

// Local command receiver for DVL service. Only the DVL's main poll loop calls
// these functions, so there is no new thread and no concurrent access to hz.
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "dvlHealth.h"

typedef struct
{
    int fd;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    DvlHealth *health;
} DvlCommandSocket;

static int dvl_command_start(DvlCommandSocket *cmd)
{
    cmd->fd = -1;
    cmd->path[0] = '\0';
    const char *custom = getenv("MARINE_DVL_SOCKET");
    int len = custom && custom[0]
                  ? snprintf(cmd->path, sizeof(cmd->path), "%s", custom)
                  : snprintf(cmd->path, sizeof(cmd->path), "/tmp/marine-dvl-%lu.sock", (unsigned long)getuid());
    if (len < 0 || (size_t)len >= sizeof(cmd->path))
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, cmd->path);

    mode_t old_mask = umask(0077);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        umask(old_mask);
        return -1;
    }
    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        int saved = errno;
        close(fd);
        umask(old_mask);
        errno = saved;
        return -1;
    }
    umask(old_mask);

    // Custom IPC socket: allow owner access only
    if (custom && custom[0])
    {
        if (chmod(cmd->path, 0600) != 0)
        {
            int saved = errno;

            perror("DVL IPC chmod");
            close(fd);
            unlink(cmd->path);

            errno = saved;
            return -1;
        }
    }

    if (listen(fd, 4) != 0 ||
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) != 0)
    {
        int saved = errno;
        close(fd);
        unlink(cmd->path);
        errno = saved;
        return -1;
    }
    // Prevent an abandoned IPC client from terminating the DVL process.
    signal(SIGPIPE, SIG_IGN);
    cmd->fd = fd;
    fprintf(stderr, "DVL command socket: %s\n", cmd->path);
    return 0;
}

static void dvl_command_stop(DvlCommandSocket *cmd)
{
    if (cmd->fd >= 0)
    {
        close(cmd->fd);
        unlink(cmd->path);
        cmd->fd = -1;
    }
}

static int64_t dvl_command_now_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
}

// Called only when the command listener becomes readable in poll().
static void dvl_command_accept(DvlCommandSocket *cmd,
                               double *hz, int64_t *interval_ns, int64_t *next_tick)
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        int client = accept(cmd->fd, NULL, NULL);
        if (client < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                return;
            perror("DVL command accept");
            return;
        }
        char input[64] = {0};
        size_t received = 0;
        // poll() + recv(..., 0) works under both strict POSIX macOS
        // and Linux builds. Bound waiting so one client cannot block DVL forever.
        for (int wait_count = 0; wait_count < 3 &&
                                 received < sizeof(input) - 1;
             ++wait_count)
        {
            struct pollfd request = {.fd = client, .events = POLLIN};
            int ready = poll(&request, 1, 50);
            if (ready <= 0 || !(request.revents & POLLIN))
                break;
            ssize_t count = recv(client, input + received,
                                 sizeof(input) - 1 - received, 0);
            if (count <= 0)
                break;
            received += (size_t)count;
            input[received] = '\0';
            if (strchr(input, '\n') != NULL)
                break;
        }
        ssize_t n = (ssize_t)received;
        const char *reply = "ERROR: invalid DVL command; use h -X (0.1..100)\n";
        char ok[256];

        if (n > 0 && strcmp(input, "?\n") == 0)
        {
            if (cmd->health != NULL)
            {
                dvl_health_report(
                    cmd->health,
                    ok,
                    sizeof(ok));
            }
            else
            {
                snprintf(
                    ok,
                    sizeof(ok),
                    "DVL STATE: UNKNOWN\n"
                    "ERROR: HEALTH_NOT_CONFIGURED\n");
            }

            reply = ok;
        }
        else if (n > 0)
        {
            input[n] = '\0';
            // Require a complete command and do not interpret partial input.
            char *newline = strchr(input, '\n');
            if (newline && newline[1] == '\0' && strncmp(input, "h -", 3) == 0 &&
                ((input[3] >= '0' && input[3] <= '9') || input[3] == '.'))
            {
                *newline = '\0';
                char *end = NULL;
                errno = 0;
                double value = strtod(input + 3, &end);
                if (errno != ERANGE && end != input + 3 && *end == '\0' &&
                    isfinite(value) && value >= 0.1 && value <= 100.0)
                {
                    *hz = value;
                    *interval_ns = (int64_t)(1000000000.0 / value + 0.5);
                    *next_tick = dvl_command_now_ns() + *interval_ns;
                    snprintf(ok, sizeof(ok), "DVL: output rate set to %.3g Hz\n", value);
                    reply = ok;
                    fprintf(stderr, "%s", reply);
                }
            }
        }
#if defined(MSG_NOSIGNAL)
        (void)send(client, reply, strlen(reply), MSG_NOSIGNAL);
#else
        (void)send(client, reply, strlen(reply), 0);
#endif
        close(client);
    }
}

#endif
