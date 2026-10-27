#ifndef MARINE_CONTROLLER_IPC_H
#define MARINE_CONTROLLER_IPC_H

/* Stage 4-B: shared, portable UNIX socket request helper for controller_tui.
 * Uses the same socket path and newline protocol as controller.c.
 * Call from the UI for Enter-submitted commands, NOT from a signal handler.
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#define MARINE_CONTROLLER_SOCKET "./controller.sock"

static int controller_ipc_request(const char *command,
                                  char *response,
                                  size_t capacity)
{
    if (response == NULL || capacity == 0)
        return -1;
    response[0] = '\0';

    if (command == NULL || command[0] == '\0' ||
        strchr(command, '\n') != NULL || strchr(command, '\r') != NULL ||
        strlen(command) > 254)
    {
        snprintf(response, capacity, "IPC ERROR: invalid command length/content");
        return -1;
    }

    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    if (strlen(MARINE_CONTROLLER_SOCKET) >= sizeof(address.sun_path))
    {
        snprintf(response, capacity, "IPC ERROR: controller socket path too long");
        return -1;
    }
    strcpy(address.sun_path, MARINE_CONTROLLER_SOCKET);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        snprintf(response, capacity, "IPC ERROR (socket): %s", strerror(errno));
        return -1;
    }

    /* Prevent SIGPIPE on writes to a disconnected controllerd (macOS/Linux). */
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE,
                     &no_sigpipe, sizeof(no_sigpipe));
#endif

    struct timeval send_limit = {.tv_sec = 3, .tv_usec = 0};
    struct timeval recv_limit = {.tv_sec = 15, .tv_usec = 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                     &send_limit, sizeof(send_limit));
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                     &recv_limit, sizeof(recv_limit));

    const char *operation = "connect";
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0)
        goto io_failure;

    char request[256];
    int request_length = snprintf(request, sizeof(request), "%s\n", command);
    if (request_length < 0 || (size_t)request_length >= sizeof(request))
    {
        snprintf(response, capacity, "IPC ERROR: request too long");
        close(fd);
        return -1;
    }

    operation = "send";
    size_t sent = 0;
    while (sent < (size_t)request_length)
    {
#ifdef MSG_NOSIGNAL
        ssize_t n = send(fd, request + sent,
                         (size_t)request_length - sent, MSG_NOSIGNAL);
#else
        ssize_t n = send(fd, request + sent,
                         (size_t)request_length - sent, 0);
#endif
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
        {
            if (n == 0)
                errno = EPIPE;
            goto io_failure;
        }
        sent += (size_t)n;
    }

    operation = "recv";
    size_t used = 0;
    for (;;)
    {
        if (used + 1 >= capacity)
        {
            snprintf(response, capacity, "IPC ERROR: response buffer too small");
            close(fd);
            return -1;
        }
        ssize_t n = recv(fd, response + used, capacity - used - 1, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            goto io_failure;
        if (n == 0)
            break;
        used += (size_t)n;
    }
    response[used] = '\0';
    close(fd);

    if (used == 0)
    {
        snprintf(response, capacity, "IPC ERROR: controllerd closed without a reply");
        return -1;
    }
    return 0;

io_failure:
    {
        int saved_errno = errno;
        close(fd);
        snprintf(response, capacity, "IPC ERROR (%s): %s",
                 operation, strerror(saved_errno));
        return -1;
    }
}

#endif /* MARINE_CONTROLLER_IPC_H */
