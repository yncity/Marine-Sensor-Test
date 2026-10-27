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
#include <sys/types.h>
#include <sys/wait.h>
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
    close(fd);
}

// Stop one systemd service
static int stop_one_unit(const char *unit)
{
    pid_t pid = fork();

    if (pid < 0)
        return -1;

    if (pid == 0)
    {
        char *const args[] = {
            "sudo",
            "-n",
            "/usr/bin/systemctl",
            "stop",
            (char *)unit,
            NULL};

        execv("/usr/bin/sudo", args);
        _exit(127);
    }

    int status;
    pid_t done;

    do
    {
        done = waitpid(pid, &status, 0);
    } while (done < 0 && errno == EINTR);

    if (done < 0)
        return -1;

    return (WIFEXITED(status) &&
            WEXITSTATUS(status) == 0)
               ? 0
               : -1;
}

// Stop Sonar, DVL and Servo
static void stop_all_units(char *response, size_t cap)
{
    const char *units[] = {
        "sonar.service",
        "dvl.service",
        "servo.service"};

    const char *names[] = {
        "SONAR",
        "DVL",
        "SERVO"};

    size_t used = 0;
    int failed = 0;

    for (size_t i = 0; i < 3; ++i)
    {
        int result = stop_one_unit(units[i]);

        if (result != 0)
            ++failed;

        const char *label =
            result == 0 ? "STOPPED" : "STOP_FAILED";

        int written = snprintf(
            response + used,
            cap - used,
            "%s: %s\n",
            names[i],
            label);

        if (written < 0 ||
            (size_t)written >= cap - used)
            return;

        used += (size_t)written;
    }

    snprintf(response + used, cap - used,
             "SYSTEM: %s\n",
             failed ? "STOP_INCOMPLETE"
                    : "ALL_DEVICES_STOPPED");
}

// Query the state of one systemd service
static void read_unit_state(
    const char *unit,
    char *out,
    size_t cap)
{
    int pipefd[2];

    if (pipe(pipefd) != 0)
    {
        snprintf(out, cap, "QUERY_ERROR");
        return;
    }

    pid_t child = fork();

    if (child < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);
        snprintf(out, cap, "QUERY_ERROR");
        return;
    }

    if (child == 0)
    {
        close(pipefd[0]);

        if (dup2(pipefd[1], STDOUT_FILENO) < 0)
            _exit(127);

        close(pipefd[1]);

        execl(
            "/usr/bin/systemctl",
            "systemctl",
            "is-active",
            unit,
            (char *)NULL);

        _exit(127);
    }

    close(pipefd[1]);

    size_t used = 0;

    while (used + 1 < cap)
    {
        ssize_t n = read(
            pipefd[0],
            out + used,
            cap - used - 1);

        if (n < 0 && errno == EINTR)
            continue;

        if (n <= 0)
            break;

        used += (size_t)n;
    }

    close(pipefd[0]);
    out[used] = '\0';

    int status = 0;
    pid_t waited;

    do
    {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0 ||
        !WIFEXITED(status) ||
        WEXITSTATUS(status) == 127)
    {
        snprintf(out, cap, "QUERY_ERROR");
        return;
    }

    while (used > 0 &&
           (out[used - 1] == '\n' ||
            out[used - 1] == '\r'))
    {
        out[--used] = '\0';
    }

    if (used == 0)
        snprintf(out, cap, "UNKNOWN");
}

// Collect service states
static void report_service_states(char *response, size_t cap)
{
    const char *units[] = {
        "sonar.service",
        "dvl.service",
        "servo.service"};

    const char *names[] = {
        "SONAR", "DVL", "SERVO"};

    const char *prefixes[] = {
        "SONAR STATE: ",
        "DVL STATE: ",
        "SERVO STATE: "};

    size_t used = 0;
    int failed = 0;
    int stopped = 0;
    int pending = 0;

    if (cap == 0)
        return;

    response[0] = '\0';

    for (size_t i = 0; i < 3; ++i)
    {
        char service[64];
        char health[512] = {0};
        char raw[512] = {0};
        char state[32] = "UNKNOWN";

        // 1. Read systemd service state
        read_unit_state(
            units[i],
            service,
            sizeof(service));

        // 2. Request device health via IPC
        if (strcmp(service, "active") == 0)
        {
            if (i == 0)
            {
                relay_sonar_command(
                    '?', raw, sizeof(raw));
            }
            else if (i == 1)
            {
                relay_dvl_command(
                    "?", raw, sizeof(raw));
            }
            else
            {
                relay_servo_command(
                    '?', raw, sizeof(raw));
            }

            // Validate the expected health response
            if (strncmp(
                    raw,
                    prefixes[i],
                    strlen(prefixes[i])) == 0 &&
                sscanf(
                    raw + strlen(prefixes[i]),
                    "%31s",
                    state) == 1)
            {
                snprintf(
                    health,
                    sizeof(health),
                    "%s",
                    raw);
            }
            else
            {
                strcpy(state, "UNKNOWN");

                snprintf(
                    health,
                    sizeof(health),
                    "%s STATE: UNKNOWN\n"
                    "ERROR: IPC_UNAVAILABLE\n"
                    "DETAIL: %.250s\n",
                    names[i],
                    raw);
            }
        }
        else if (strcmp(service, "inactive") == 0)
        {
            strcpy(state, "STOPPED");

            snprintf(
                health,
                sizeof(health),
                "%s STATE: STOPPED\n"
                "ERROR: NONE\n",
                names[i]);
        }
        else if (strcmp(service, "failed") == 0)
        {
            strcpy(state, "ERROR");

            snprintf(
                health,
                sizeof(health),
                "%s STATE: ERROR\n"
                "ERROR: SERVICE_FAILED\n",
                names[i]);
        }
        else if (strcmp(service, "activating") == 0)
        {
            strcpy(state, "STARTING");

            snprintf(
                health,
                sizeof(health),
                "%s STATE: STARTING\n"
                "ERROR: NONE\n",
                names[i]);
        }
        else
        {
            snprintf(
                health,
                sizeof(health),
                "%s STATE: UNKNOWN\n"
                "ERROR: SERVICE_STATUS_UNKNOWN\n",
                names[i]);
        }

        // 3. Aggregate system state
        if (strcmp(state, "STOPPED") == 0)
        {
            ++stopped;
        }
        else if (strcmp(state, "ERROR") == 0 ||
                 strcmp(state, "UNKNOWN") == 0 ||
                 strcmp(state, "DEGRADED") == 0)
        {
            ++failed;
        }
        else if (strcmp(state, "RUNNING") != 0 &&
                 strcmp(state, "READY") != 0)
        {
            ++pending;
        }

        // 4. Append device report
        int written = snprintf(
            response + used,
            cap - used,
            "%s SERVICE: %s\n%s\n",
            names[i],
            service,
            health);

        if (written < 0 ||
            (size_t)written >= cap - used)
            return;

        used += (size_t)written;
    }

    const char *summary =
        stopped == 3 ? "STOPPED" : failed           ? "DEGRADED"
                               : stopped || pending ? "PARTIAL"
                                                    : "OPERATIONAL";

    (void)failed;
    (void)stopped;
    (void)pending;

    (void)snprintf(
        response + used,
        cap - used,
        "SYSTEM STATE: %s\n",
        summary);
}
static const char *dispatch(const char *command)
{
    static char response[2048];
    if (strcmp(command, "ping") == 0)
        return "PONG: controllerd is responding\n";
    if (strcmp(command, "help") == 0)
        return "AVAILABLE: ping, help\n"
               "AVAILABLE: Sonar r/e/l/v/1-8/p/t/c\n"
               "AVAILABLE: DVL h -x (0.1..100 Hz)\n"
               "AVAILABLE: Servo u/i/o\n"
               "AVAILABLE: q (stop all devices)\n"
               "AVAILABLE: sonar-health\n"
               "AVAILABLE: dvl-health\n"
               "AVAILABLE: servo-health\n"
               "AVAILABLE: status, q\n";
    if (strcmp(command, "status") == 0)
    {
        report_service_states(
            response,
            sizeof(response));

        return response;
    }
    if (strcmp(command, "q") == 0)
    {
        stop_all_units(response, sizeof(response));
        return response;
    }
    if (strncmp(command, "h -", 3) == 0)
    {
        relay_dvl_command(command, response, sizeof(response));
        return response;
    }
    if (strcmp(command, "dvl-health") == 0)
    {
        relay_dvl_command(
            "?",
            response,
            sizeof(response));

        return response;
    }
    if (strcmp(command, "sonar-health") == 0)
    {
        relay_sonar_command(
            '?',
            response,
            sizeof(response));

        return response;
    }
    if (strcmp(command, "servo-health") == 0)
    {
        relay_servo_command(
            '?',
            response,
            sizeof(response));

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

    // Automatically reap completed request processes.
    struct sigaction reap = {0};

    reap.sa_handler = SIG_IGN;

#ifdef SA_NOCLDWAIT
    reap.sa_flags = SA_NOCLDWAIT;
#endif

    sigemptyset(&reap.sa_mask);

    if (sigaction(SIGCHLD, &reap, NULL) != 0)
    {
        perror("sigaction SIGCHLD");
        return EXIT_FAILURE;
    }

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

        pid_t pid = fork();

        if (pid < 0)
        {
            perror("fork");
            close(client);
            continue;
        }

        if (pid == 0)
        {
            // Child process handles this client only.
            close(server);

            /*
             * Restore normal child-process waiting.
             * Required by stop_one_unit(), which uses waitpid().
             */
            signal(SIGCHLD, SIG_DFL);

            handle_client(client);

            close(client);

            _exit(EXIT_SUCCESS);
        }

        // Parent can immediately accept another client.
        close(client);
    }
    close(server);
    unlink(path);
    puts("controllerd stopped");
    return EXIT_SUCCESS;
}
