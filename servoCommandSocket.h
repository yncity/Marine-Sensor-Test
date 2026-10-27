#ifndef SERVO_COMMAND_SOCKET_H
#define SERVO_COMMAND_SOCKET_H

/* Stage 3-B-3: one-byte local IPC receiver for the existing servo service.
 * Portable POSIX C (macOS/Linux); the servo main loop remains the only PWM writer.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

typedef struct {
    int fd;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
} ServoCommandSocket;

static int servo_command_start(ServoCommandSocket *cmd)
{
    cmd->fd = -1;
    cmd->path[0] = '\0';
    const char *custom = getenv("MARINE_SERVO_SOCKET");
    int written = (custom && custom[0])
        ? snprintf(cmd->path, sizeof(cmd->path), "%s", custom)
        : snprintf(cmd->path, sizeof(cmd->path), "/tmp/marine-servo-%lu.sock",
                   (unsigned long)getuid());
    if (written < 0 || (size_t)written >= sizeof(cmd->path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, cmd->path);

    mode_t original_mask = umask(0077);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        umask(original_mask);
        return -1;
    }
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        int saved = errno;
        close(fd);
        umask(original_mask);
        errno = saved;
        return -1;
    }
    umask(original_mask);
    if (listen(fd, 4) != 0) {
        int saved = errno;
        close(fd);
        unlink(cmd->path);
        errno = saved;
        return -1;
    }
    /* Avoid a connected client that disconnects early killing the servo. */
    signal(SIGPIPE, SIG_IGN);
    cmd->fd = fd;
    fprintf(stderr, "Servo command socket: %s\n", cmd->path);
    return 0;
}

static void servo_command_stop(ServoCommandSocket *cmd)
{
    if (cmd->fd >= 0) {
        close(cmd->fd);
        unlink(cmd->path);
        cmd->fd = -1;
    }
}

/* Returns 0 for idle/handled request; -1 for listener failure.
 * apply_key is invoked ONLY for u/i/o from this (main) thread.
 * All command clients have at most 50 ms to submit one byte.
 */
static int servo_command_poll(ServoCommandSocket *cmd,
                              int (*apply_key)(char key, void *context),
                              void *context)
{
    struct pollfd incoming = {.fd = cmd->fd, .events = POLLIN, .revents = 0};
    int ready = poll(&incoming, 1, 100);
    if (ready < 0) {
        if (errno == EINTR)
            return 0;
        return -1;
    }
    if (ready == 0)
        return 0;
    if (incoming.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        errno = EIO;
        return -1;
    }
    if (!(incoming.revents & POLLIN))
        return 0;

    int client = accept(cmd->fd, NULL, NULL);
    if (client < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        return -1;
    }

    struct pollfd request = {.fd = client, .events = POLLIN, .revents = 0};
    int got = poll(&request, 1, 50);
    char key = 0;
    const char *reply = "ERROR: invalid Servo command\n";
    if (got > 0 && (request.revents & POLLIN) && recv(client, &key, 1, 0) == 1) {
        if (key == 'u' || key == 'i' || key == 'o') {
            if (apply_key(key, context) == 0) {
                switch (key) {
                    case 'u': reply = "SERVO: 500 us applied\n"; break;
                    case 'i': reply = "SERVO: 1520 us applied\n"; break;
                    case 'o': reply = "SERVO: 2500 us applied\n"; break;
                }
            } else {
                reply = "SERVO_ERROR: PWM update failed\n";
            }
        }
    }
#if defined(MSG_NOSIGNAL)
    (void)send(client, reply, strlen(reply), MSG_NOSIGNAL);
#else
    (void)send(client, reply, strlen(reply), 0);
#endif
    close(client);
    return 0;
}

#endif
