#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "dvlCommandSocket.h"

#define DVL_IP "192.168.1.200"
#define DVL_PORT 16171
#define MAX_LINE_BYTES (1024 * 1024)

// Default output frequency
#define DEFAULT_OUTPUT_HZ 2.0

// Termination flag
static volatile sig_atomic_t g_stop_requested = 0;

static void handle_stop_signal(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}
// ─── Existing JSON pretty-printer: preserve value spellings and array structure ───
static void skip_space(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')
        ++(*p);
}

static void indent(int level)
{
    for (int i = 0; i < level * 4; ++i)
        putchar(' ');
}

static void print_string(const char **p)
{
    putchar(*(*p)++); // opening double quote
    while (**p)
    {
        char c = *(*p)++;
        putchar(c);
        if (c == '\\' && **p)
        {
            putchar(*(*p)++);
        }
        else if (c == '"')
        {
            break;
        }
    }
}

static void print_value(const char **p, int level, int covariance);

static void print_object(const char **p, int level)
{
    putchar(*(*p)++); // {
    skip_space(p);

    if (**p == '}')
    {
        putchar(*(*p)++);
        return;
    }

    for (;;)
    {
        putchar('\n');
        indent(level + 1);

        const char *key = *p;
        print_string(p);
        size_t key_len = (size_t)(*p - key);
        int covariance = key_len == sizeof("\"covariance\"") - 1 &&
                         strncmp(key, "\"covariance\"", key_len) == 0;

        skip_space(p);
        if (**p != ':')
            return; // malformed JSON (DVL should always send valid JSON)
        ++(*p);
        fputs(": ", stdout);
        skip_space(p);
        print_value(p, level + 1, covariance);
        skip_space(p);

        if (**p != ',')
            break;
        putchar(',');
        ++(*p);
        skip_space(p);
    }

    putchar('\n');
    indent(level);
    if (**p == '}')
        putchar(*(*p)++);
}

static void print_matrix_row(const char **p)
{
    putchar(*(*p)++); // [
    skip_space(p);
    while (**p && **p != ']')
    {
        print_value(p, 0, 0);
        skip_space(p);
        if (**p != ',')
            break;
        fputs(", ", stdout);
        ++(*p);
        skip_space(p);
    }
    if (**p == ']')
        putchar(*(*p)++);
}

static void print_array(const char **p, int level, int covariance)
{
    putchar(*(*p)++); // [
    skip_space(p);
    if (**p == ']')
    {
        putchar(*(*p)++);
        return;
    }

    for (;;)
    {
        putchar('\n');
        indent(level + 1);
        if (covariance && **p == '[')
            print_matrix_row(p);
        else
            print_value(p, level + 1, 0);
        skip_space(p);
        if (**p != ',')
            break;
        putchar(',');
        ++(*p);
        skip_space(p);
    }
    putchar('\n');
    indent(level);
    if (**p == ']')
        putchar(*(*p)++);
}

static void print_value(const char **p, int level, int covariance)
{
    skip_space(p);
    if (**p == '{')
        print_object(p, level);
    else if (**p == '[')
        print_array(p, level, covariance);
    else if (**p == '"')
        print_string(p);
    else
        while (**p && strchr(",]} \t\r\n", **p) == NULL)
            putchar(*(*p)++);
}

static void print_json(const char *line)
{
    const char *p = line;
    skip_space(&p);
    if (*p != '{')
        return;
    print_value(&p, 0, 0);
    fputs("\n\n", stdout);
}

// ─── Sampling: keep latest JSON of each DVL message type ───
typedef struct
{
    char *velocity;
    char *position_local;
    char *other;
    int new_velocity;
    int new_position_local;
    int new_other;
} Samples;

static int is_type(const char *json, const char *wanted)
{
    // DVL JSON places 'type' at the top level; allow spaces around ':'.
    const char *p = strstr(json, "\"type\"");
    if (!p)
        return 0;
    p += strlen("\"type\"");
    while (isspace((unsigned char)*p))
        ++p;
    if (*p++ != ':')
        return 0;
    while (isspace((unsigned char)*p))
        ++p;
    if (*p++ != '"')
        return 0;
    size_t n = strlen(wanted);
    return strncmp(p, wanted, n) == 0 && p[n] == '"';
}

static int replace(char **dst, const char *value)
{
    char *copy = strdup(value);
    if (!copy)
        return -1;
    free(*dst);
    *dst = copy;
    return 0;
}

static int store_message(Samples *samples, const char *line)
{
    const char *p = line;
    skip_space(&p);
    if (*p != '{')
        return 0;

    if (is_type(p, "velocity"))
    {
        if (replace(&samples->velocity, p) < 0)
            return -1;
        samples->new_velocity = 1;
    }
    else if (is_type(p, "position_local"))
    {
        if (replace(&samples->position_local, p) < 0)
            return -1;
        samples->new_position_local = 1;
    }
    else
    {
        if (replace(&samples->other, p) < 0)
            return -1;
        samples->new_other = 1;
    }
    return 0;
}

static void output_newest(Samples *samples)
{
    // Print each type at most once per interval, only if it has new data.
    if (samples->new_velocity)
    {
        print_json(samples->velocity);
        samples->new_velocity = 0;
    }
    if (samples->new_position_local)
    {
        print_json(samples->position_local);
        samples->new_position_local = 0;
    }
    if (samples->new_other)
    {
        print_json(samples->other);
        samples->new_other = 0;
    }
    fflush(stdout);
}

static int64_t monotonic_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int read_rate(double *hz)
{
    char input[128];
    printf("몇 Hz로 출력할까요? (0.1 ~ 100): ");
    fflush(stdout);
    if (!fgets(input, sizeof(input), stdin))
        return -1;

    char *end;
    errno = 0;
    double value = strtod(input, &end);
    while (isspace((unsigned char)*end))
        ++end;
    if (errno == ERANGE || !isfinite(value) || end == input || *end != '\0' ||
        value < 0.1 || value > 100.0)
    {
        fprintf(stderr, "0.1 ~ 100 사이의 Hz 값을 입력해주세요 (예: 1, 2, 5, 10).\n");
        return -1;
    }
    *hz = value;
    return 0;
}

int main(int argc, char *argv[])
{

    // Check service mode
    int service_mode =
        (argc > 1 && strcmp(argv[1], "--service") == 0);

    int arg_index = service_mode ? 2 : 1;

    if (argc > arg_index + 2)
    {
        fprintf(stderr, "Usage: %s [--service] [IP] [PORT]\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Default output frequency: 5 Hz
    double hz = DEFAULT_OUTPUT_HZ;

    // Manual mode: read Hz from keyboard
    if (!service_mode)
    {
        if (read_rate(&hz) < 0)
            return EXIT_FAILURE;
    }

    // Service mode: handle stop signals
    if (service_mode)
    {
        struct sigaction sa = {0};

        sa.sa_handler = handle_stop_signal;
        sigemptyset(&sa.sa_mask);

        if (sigaction(SIGTERM, &sa, NULL) < 0 ||
            sigaction(SIGINT, &sa, NULL) < 0)
        {
            perror("sigaction");
            return EXIT_FAILURE;
        }
    }

    int64_t interval_ns =
        (int64_t)(1000000000.0 / hz + 0.5);

    // IP and port settings
    const char *ip =
        argc > arg_index ? argv[arg_index] : DVL_IP;

    int port =
        argc > arg_index + 1
            ? atoi(argv[arg_index + 1])
            : DVL_PORT;
    if (port < 1 || port > 65535)
    {
        fprintf(stderr, "Invalid port: %d\n", port);
        return EXIT_FAILURE;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid IPv4 address: %s\n", ip);
        close(sock);
        return EXIT_FAILURE;
    }
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        close(sock);
        return EXIT_FAILURE;
    }
    printf("DVL 연결 성공: %s:%d\n", ip, port);
    printf("출력 주파수: %.2f Hz | 출력 간격: %.1f ms\n", hz, 1000.0 / hz);
    printf("각 출력 시점의 최신 velocity / position_local (평균 계산 없음)\n\n");
    fflush(stdout);

    Samples samples = {0};
    size_t capacity = 4096, used = 0;
    char *line = malloc(capacity);
    if (!line)
    {
        perror("malloc");
        close(sock);
        return EXIT_FAILURE;
    }
    int discarding = 0;
    int exit_code = EXIT_SUCCESS;

    // DVL health tracking
    DvlHealth dvl_health;
    dvl_health_init(&dvl_health);

    dvl_health_on_connect(
        &dvl_health,
        dvl_health_now_ns());

    // DVL command socket
    DvlCommandSocket dvlCommandSocket = {.fd = -1};
    dvlCommandSocket.health = &dvl_health;
    if (service_mode && dvl_command_start(&dvlCommandSocket) != 0)
    {
        perror("DVL command socket");
        exit_code = EXIT_FAILURE;
        goto cleanup;
    }
    int64_t next_tick = monotonic_ns() + interval_ns;

    while (!g_stop_requested)
    {
        int64_t now = monotonic_ns();
        if (now >= next_tick)
        {
            output_newest(&samples);
            // Skip missed ticks instead of printing outdated samples repeatedly.
            next_tick += ((now - next_tick) / interval_ns + 1) * interval_ns;
            continue;
        }

        int64_t remaining_ns = next_tick - now;

        // Calculate poll timeout
        int timeout_ms =
            (int)((remaining_ns + 999999) / 1000000);

        // Monitor TCP socket and DVL command socket
        struct pollfd pfds[2] = {
            {.fd = sock, .events = POLLIN},
            {.fd = dvlCommandSocket.fd, .events = POLLIN}};

        nfds_t nfds = service_mode ? 2 : 1;

        int rc = poll(pfds, nfds, timeout_ms);

        if (rc < 0)
        {
            if (errno == EINTR)
                continue;

            perror("poll");
            exit_code = EXIT_FAILURE;
            break;
        }

        if (rc == 0)
            continue;

        // DVL command received
        if (service_mode && (pfds[1].revents & POLLIN))
        {
            dvl_command_accept(
                &dvlCommandSocket,
                &hz,
                &interval_ns,
                &next_tick);
        }

        // TCP socket error
        if (pfds[0].revents & (POLLERR | POLLNVAL))
        {
            fprintf(stderr, "TCP socket error.\n");
            exit_code = EXIT_FAILURE;
            break;
        }

        // Command only: no TCP data to receive
        if (!(pfds[0].revents & (POLLIN | POLLHUP)))
            continue;
        char chunk[8192];
        ssize_t n = recv(sock, chunk, sizeof(chunk), 0);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            perror("recv");
            exit_code = EXIT_FAILURE;
            break;
        }
        if (n == 0)
        {
            fprintf(stderr, "DVL TCP 연결이 종료되었습니다.\n");

            output_newest(&samples);

            if (service_mode && !g_stop_requested)
            {
                exit_code = EXIT_FAILURE;
            }

            break;
        }

        // Record received TCP bytes
        dvl_health_on_bytes(
            &dvl_health,
            (size_t)n,
            dvl_health_now_ns());

        // TCP is a byte stream. Split at newline before interpreting each JSON message.
        for (ssize_t i = 0; i < n; ++i)
        {
            char c = chunk[i];
            if (c == '\n')
            {
                if (!discarding && used > 0)
                {
                    line[used] = '\0';

                    // Preserve existing JSON storage
                    if (store_message(&samples, line) != 0)
                    {
                        perror("strdup");
                        exit_code = EXIT_FAILURE;
                        goto cleanup;
                    }

                    // Record the completed message
                    dvl_health_on_line(
                        &dvl_health,
                        line,
                        dvl_health_now_ns(),
                        is_type(line, "velocity"));
                }
                used = 0;
                discarding = 0;
                continue;
            }
            if (discarding)
                continue;
            if (used + 1 >= MAX_LINE_BYTES)
            {
                fprintf(stderr, "JSON 메시지가 너무 길어 건너뜁니다.\n");

                dvl_health_on_oversized(&dvl_health);

                used = 0;
                discarding = 1;
                continue;
            }
            if (used + 1 >= capacity)
            {
                size_t new_capacity = capacity * 2;
                char *new_line = realloc(line, new_capacity);
                if (!new_line)
                {
                    perror("realloc");
                    exit_code = EXIT_FAILURE;
                    goto cleanup;
                }
                line = new_line;
                capacity = new_capacity;
            }
            line[used++] = c;
        }
    }

cleanup:
    dvl_command_stop(&dvlCommandSocket);
    free(samples.velocity);
    free(samples.position_local);
    free(samples.other);
    free(line);
    close(sock);
    return exit_code;
}
