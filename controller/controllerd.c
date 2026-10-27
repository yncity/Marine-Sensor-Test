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

static const char *sonar_socket_path(void)
{
    const char *custom = getenv("MARINE_SONAR_SOCKET");
    if (custom && custom[0])
        return custom;
    static char path[108];
    snprintf(path, sizeof(path), "/tmp/marine-sonar-%lu.sock", (unsigned long)getuid());
    return path;
}

static int is_allowed_sonar_key(char key)
{
    // Only non-interactive Sonar commands. Exclude x (3-C shutdown), z (GUI),
    // n (persistent network IP), d (persistent factory defaults).
    return strchr("relv12345678ptc", key) != NULL;
}

static void relay_sonar_command(char key, char *reply, size_t cap)
{
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    const char *path = sonar_socket_path();
    if (strlen(path) >= sizeof(address.sun_path))
    {
        snprintf(reply, cap, "ERROR: Sonar socket path too long\n");
        return;
    }
    strcpy(address.sun_path, path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        snprintf(reply, cap, "ERROR: Sonar IPC socket failed: %s\n", strerror(errno));
        return;
    }

    struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        snprintf(reply, cap, "SONAR_OFFLINE: %s\n", strerror(errno));
        close(fd);
        return;
    }
    // A single byte per connection; Sonar applies the key from the SDK thread.
#if defined(MSG_NOSIGNAL)
    ssize_t sent = send(fd, &key, 1, MSG_NOSIGNAL);
#else
    ssize_t sent = send(fd, &key, 1, 0);
#endif
    if (sent != 1)
    {
        snprintf(reply, cap, "ERROR: Could not send Sonar command\n");
        close(fd);
        return;
    }

    ssize_t n = recv(fd, reply, cap - 1, 0);
    if (n > 0)
        reply[n] = '\0';
    else
        snprintf(reply, cap, "ERROR: Sonar did not acknowledge command\n");
    close(fd);
}

static const char *dvl_socket_path(void)
{
    const char *custom = getenv("MARINE_DVL_SOCKET");
    if (custom && custom[0])
        return custom;
    static char path[108];
    snprintf(path, sizeof(path), "/tmp/marine-dvl-%lu.sock", (unsigned long)getuid());
    return path;
}

static void relay_dvl_command(const char *command, char *reply, size_t cap)
{
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;
    const char *path = dvl_socket_path();
    if (strlen(path) >= sizeof(address.sun_path))
    {
        snprintf(reply, cap, "ERROR: DVL socket path too long\n");
        return;
    }
    strcpy(address.sun_path, path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        snprintf(reply, cap, "ERROR: DVL socket creation failed: %s\n", strerror(errno));
        return;
    }
    struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        snprintf(reply, cap, "DVL_OFFLINE: %s\n", strerror(errno));
        close(fd);
        return;
    }
    char payload[128];
    int n = snprintf(payload, sizeof(payload), "%s\n", command);
    if (n < 0 || (size_t)n >= sizeof(payload))
    {
        snprintf(reply, cap, "ERROR: DVL command too long\n");
        close(fd);
        return;
    }
#if defined(MSG_NOSIGNAL)
    ssize_t written = send(fd, payload, (size_t)n, MSG_NOSIGNAL);
#else
    ssize_t written = send(fd, payload, (size_t)n, 0);
#endif
    if (written != n)
    {
        snprintf(reply, cap, "ERROR: DVL command send failed\n");
        close(fd);
        return;
    }
    ssize_t read_bytes = recv(fd, reply, cap - 1, 0);
    if (read_bytes > 0)
        reply[read_bytes] = '\0';
    else
        snprintf(reply, cap, "ERROR: DVL did not acknowledge command\n");
    close(fd);
}

static const char *servo_socket_path(void)
{
    const char *custom = getenv("MARINE_SERVO_SOCKET");

    if (custom && custom[0])
        return custom;

    static char path[108];

    snprintf(path, sizeof(path),
             "/tmp/marine-servo-%lu.sock",
             (unsigned long)getuid());

    return path;
}

static void relay_servo_command(char key, char *reply, size_t cap)
{
    struct sockaddr_un address = {0};
    address.sun_family = AF_UNIX;

    const char *path = servo_socket_path();

    if (strlen(path) >= sizeof(address.sun_path))
    {
        snprintf(reply, cap, "ERROR: Servo socket path too long\n");
        return;
    }

    strcpy(address.sun_path, path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd < 0)
    {
        snprintf(reply, cap,
                 "ERROR: Servo socket creation failed: %s\n",
                 strerror(errno));
        return;
    }

    struct timeval timeout = {
        .tv_sec = 2,
        .tv_usec = 0};

    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                     &timeout, sizeof(timeout));

    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                     &timeout, sizeof(timeout));

    if (connect(fd, (struct sockaddr *)&address,
                sizeof(address)) != 0)
    {
        snprintf(reply, cap,
                 "SERVO_OFFLINE: %s\n", strerror(errno));
        close(fd);
        return;
    }

#if defined(MSG_NOSIGNAL)
    ssize_t written = send(fd, &key, 1, MSG_NOSIGNAL);
#else
    ssize_t written = send(fd, &key, 1, 0);
#endif

    if (written != 1)
    {
        snprintf(reply, cap, "ERROR: Servo command send failed\n");
        close(fd);
        return;
    }

    ssize_t count = recv(fd, reply, cap - 1, 0);

    if (count > 0)
        reply[count] = '\0';
    else
        snprintf(reply, cap,
                 "ERROR: Servo did not acknowledge command\n");
    ƒ

        close(fd);
}

static const char *dispatch(const char *command)
{
    static char response[512];
    if (strcmp(command, "ping") == 0)
        return "PONG: controllerd is responding\n";
    if (strcmp(command, "help") == 0)
        return "AVAILABLE: ping, help\n"
               "AVAILABLE: Sonar r/e/l/v/1-8/p/t/c\n"
               "AVAILABLE: DVL h -x (0.1..100 Hz)\n"
               "AVAILABLE: Servo u/i/o\n"
               "PENDING: status, q\n";
    if (strcmp(command, "status") == 0)
        return "NOT_IMPLEMENTED: device health/status is not connected yet\n";
    if (strcmp(command, "q") == 0)
        return "NOT_IMPLEMENTED: global stop is not connected; no device was stopped\n";
    if (strncmp(command, "h -", 3) == 0)
    {
        relay_dvl_command(command, response, sizeof(response));
        return response;
    }
    if (strlen(command) == 1 &&
        strchr("uio", command[0]) != NULL)
    {
        relay_servo_command(
            command[0],
            response,
            sizeof(response));

        return response;
    }
    if (strlen(command) == 1 && is_allowed_sonar_key(command[0]))
    {
        relay_sonar_command(command[0], response, sizeof(response));
        return response;
    }
    if (strcmp(command, "z") == 0)
        return "DEFERRED: remote Sonar GUI is a later development stage\n";
    if (strcmp(command, "n") == 0 || strcmp(command, "d") == 0)
        return "DENIED: persistent device configuration is not a general command\n";
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
#if defined(MSG_NOSIGNAL)
        ssize_t n = send(client, response, remaining, MSG_NOSIGNAL);
#else
        ssize_t n = send(client, response, remaining, 0);
#endif
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
