#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define DEFAULT_SOCKET "./controller.sock"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s COMMAND [ARGUMENT...]\n", argv[0]);
        return EXIT_FAILURE;
    }
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    if (strlen(DEFAULT_SOCKET) >= sizeof(address.sun_path))
        return EXIT_FAILURE;
    strcpy(address.sun_path, DEFAULT_SOCKET);

    char command[256] = {0};
    size_t length = 0;
    for (int i = 1; i < argc; ++i)
    {
        size_t word_length = strlen(argv[i]);
        if (length + word_length + 2 > sizeof(command))
        {
            fprintf(stderr, "Command too long\n");
            return EXIT_FAILURE;
        }
        if (i > 1)
            command[length++] = ' ';
        memcpy(command + length, argv[i], word_length);
        length += word_length;
    }
    command[length++] = '\n';

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        perror("connect (is controllerd running in this directory?)");
        close(fd);
        return EXIT_FAILURE;
    }
    size_t sent = 0;
    while (sent < length)
    {
        ssize_t n = write(fd, command + sent, length - sent);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
        {
            perror("write");
            close(fd);
            return EXIT_FAILURE;
        }
        sent += (size_t)n;
    }

    char response[1024];
    for (;;)
    {
        ssize_t n = read(fd, response, sizeof(response));
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
        {
            perror("read");
            close(fd);
            return EXIT_FAILURE;
        }
        if (n == 0)
            break;
        if (fwrite(response, 1, (size_t)n, stdout) != (size_t)n)
        {
            close(fd);
            return EXIT_FAILURE;
        }
    }
    close(fd);
    return EXIT_SUCCESS;
}
