#ifndef MARINE_CONTROLLER_JOURNAL_H
#define MARINE_CONTROLLER_JOURNAL_H

/* Stage 5-B: collect actual systemd journal messages for the three services.
 * - One independent journalctl follower per service.
 * - Only stdout of journalctl (-o cat) enters the device log ring buffer.
 * - journalctl stderr is retained separately as a collector diagnostic.
 * - No ncurses access and no device/IPC commands.
 * - Requires Linux with systemd-journald for actual operation.
 */

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CONTROLLER_JOURNAL_DEVICES 3
#define CONTROLLER_JOURNAL_KEEP_LINES 160
#define CONTROLLER_JOURNAL_LINE_LENGTH 1024
#define CONTROLLER_JOURNAL_DIAGNOSTIC_LENGTH 192

/* These indexes are also the left-to-right order of the ncurses panes. */
enum ControllerJournalDevice
{
    CONTROLLER_JOURNAL_SONAR = 0,
    CONTROLLER_JOURNAL_DVL = 1,
    CONTROLLER_JOURNAL_SERVO = 2
};

typedef struct
{
    pthread_mutex_t mutex;
    pthread_t thread;
    _Atomic int stop;
    int thread_started;
    int online;
    unsigned long version;
    size_t write_pos;
    size_t count;
    char diagnostic[CONTROLLER_JOURNAL_DIAGNOSTIC_LENGTH];
    char lines[CONTROLLER_JOURNAL_KEEP_LINES][CONTROLLER_JOURNAL_LINE_LENGTH];
} ControllerJournalStream;

typedef struct
{
    ControllerJournalStream stream[CONTROLLER_JOURNAL_DEVICES];
} ControllerJournalMonitor;

typedef struct
{
    ControllerJournalStream *stream;
    const char *unit;
} ControllerJournalWorkerArg;

static const char *const controller_journal_units[CONTROLLER_JOURNAL_DEVICES] = {
    "sonar.service", "dvl.service", "servo.service"
};

static void controller_journal_set_diagnostic(
    ControllerJournalStream *stream, const char *text, int online)
{
    pthread_mutex_lock(&stream->mutex);
    if (online >= 0)
        stream->online = online;
    snprintf(stream->diagnostic, sizeof(stream->diagnostic), "%s", text);
    ++stream->version;
    pthread_mutex_unlock(&stream->mutex);
}

static void controller_journal_store_line(ControllerJournalStream *stream,
                                          const char *line)
{
    pthread_mutex_lock(&stream->mutex);
    snprintf(stream->lines[stream->write_pos],
             CONTROLLER_JOURNAL_LINE_LENGTH, "%s", line);
    stream->write_pos = (stream->write_pos + 1) % CONTROLLER_JOURNAL_KEEP_LINES;
    if (stream->count < CONTROLLER_JOURNAL_KEEP_LINES)
        ++stream->count;
    ++stream->version;
    pthread_mutex_unlock(&stream->mutex);
}

/* Incremental line assembly; incomplete lines are flushed at EOF only. */
typedef struct
{
    char text[CONTROLLER_JOURNAL_LINE_LENGTH];
    size_t used;
    int truncated;
} ControllerJournalLine;

static void controller_journal_flush_line(ControllerJournalStream *stream,
                                           ControllerJournalLine *line)
{
    if (line->truncated)
    {
        const char suffix[] = " [truncated]";
        const size_t n = sizeof(suffix) - 1;
        size_t start = CONTROLLER_JOURNAL_LINE_LENGTH - 1 - n;
        memcpy(line->text + start, suffix, n);
        line->used = start + n;
    }
    line->text[line->used] = '\0';
    controller_journal_store_line(stream, line->text);
    line->used = 0;
    line->truncated = 0;
}

static void controller_journal_feed_log(ControllerJournalStream *stream,
                                        ControllerJournalLine *line,
                                        const char *data, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        unsigned char ch = (unsigned char)data[i];
        if (ch == '\n')
        {
            controller_journal_flush_line(stream, line);
        }
        else if (ch == '\r')
        {
            /* journalctl normally emits LF; ignore CR if present. */
        }
        else if (ch == '\t')
        {
            if (line->used + 1 < sizeof(line->text))
                line->text[line->used++] = ' ';
            else
                line->truncated = 1;
        }
        else if (ch >= 32 && ch != 127) /* Preserve UTF-8 bytes. */
        {
            if (line->used + 1 < sizeof(line->text))
                line->text[line->used++] = (char)ch;
            else
                line->truncated = 1;
        }
    }
}

/* stderr from journalctl belongs to collector diagnostics, not device logs. */
static void controller_journal_feed_error(ControllerJournalStream *stream,
                                          char *buffer, size_t *used,
                                          const char *data, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        unsigned char ch = (unsigned char)data[i];
        if (ch == '\n' || ch == '\r')
        {
            if (*used > 0)
            {
                buffer[*used] = '\0';
                controller_journal_set_diagnostic(stream, buffer, -1);
                *used = 0;
            }
        }
        else if (ch >= 32 && ch != 127 &&
                 *used + 1 < CONTROLLER_JOURNAL_DIAGNOSTIC_LENGTH)
        {
            buffer[(*used)++] = (char)ch;
        }
    }
}

static void controller_journal_reap(pid_t child, int stopping)
{
    if (child <= 0)
        return;

    if (stopping)
        (void)kill(child, SIGTERM); /* Only our journalctl child, never a service. */

    int status;
    for (int i = 0; i < 20; ++i)
    {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child || (result < 0 && errno == ECHILD))
            return;
        if (result < 0 && errno != EINTR)
            return;
        struct timespec pause_time = {.tv_sec = 0, .tv_nsec = 50000000};
        (void)nanosleep(&pause_time, NULL);
    }
    (void)kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) { }
}

static void *controller_journal_worker(void *arg)
{
    ControllerJournalWorkerArg *worker = (ControllerJournalWorkerArg *)arg;
    ControllerJournalStream *stream = worker->stream;
    const char *unit = worker->unit;
    free(worker);

    int output[2];
    int errors[2];
    if (pipe(output) != 0)
    {
        controller_journal_set_diagnostic(stream, "Cannot create journal stdout pipe", 0);
        return NULL;
    }
    if (pipe(errors) != 0)
    {
        close(output[0]); close(output[1]);
        controller_journal_set_diagnostic(stream, "Cannot create journal stderr pipe", 0);
        return NULL;
    }

    pid_t child = fork();
    if (child < 0)
    {
        close(output[0]); close(output[1]);
        close(errors[0]); close(errors[1]);
        controller_journal_set_diagnostic(stream, "Cannot fork journalctl", 0);
        return NULL;
    }
    if (child == 0)
    {
        close(output[0]);
        close(errors[0]);
        if (dup2(output[1], STDOUT_FILENO) < 0 ||
            dup2(errors[1], STDERR_FILENO) < 0)
            _exit(127);
        close(output[1]);
        close(errors[1]);
        char *const argv[] = {
            "journalctl", "--no-pager", "--boot", "--follow",
            "--lines=20", "--output=cat", "--unit", (char *)unit, NULL
        };
        execvp("journalctl", argv);
        static const char fail[] = "Could not execute journalctl\n";
        (void)write(STDERR_FILENO, fail, sizeof(fail) - 1);
        _exit(127);
    }

    close(output[1]);
    close(errors[1]);
    controller_journal_set_diagnostic(stream, "Following systemd journal", 1);

    ControllerJournalLine line = {{0}, 0, 0};
    char err_line[CONTROLLER_JOURNAL_DIAGNOSTIC_LENGTH] = {0};
    size_t err_used = 0;
    int out_open = 1;
    int err_open = 1;

    while (!atomic_load(&stream->stop) && (out_open || err_open))
    {
        struct pollfd fds[2];
        nfds_t nfds = 0;
        if (out_open)
        {
            fds[nfds].fd = output[0];
            fds[nfds].events = POLLIN | POLLHUP;
            fds[nfds].revents = 0;
            ++nfds;
        }
        if (err_open)
        {
            fds[nfds].fd = errors[0];
            fds[nfds].events = POLLIN | POLLHUP;
            fds[nfds].revents = 0;
            ++nfds;
        }
        int result = poll(fds, nfds, 200);
        if (result < 0)
        {
            if (errno == EINTR)
                continue;
            controller_journal_set_diagnostic(stream, "journalctl pipe poll failed", 0);
            break;
        }
        if (result == 0)
            continue;

        for (nfds_t i = 0; i < nfds; ++i)
        {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)))
                continue;
            char data[1024];
            ssize_t n = read(fds[i].fd, data, sizeof(data));
            const int is_output = (fds[i].fd == output[0]);
            if (n > 0)
            {
                if (is_output)
                    controller_journal_feed_log(stream, &line, data, (size_t)n);
                else
                    controller_journal_feed_error(stream, err_line, &err_used, data, (size_t)n);
            }
            else if (n == 0 || (n < 0 && errno != EINTR))
            {
                if (is_output)
                {
                    close(output[0]);
                    out_open = 0;
                }
                else
                {
                    close(errors[0]);
                    err_open = 0;
                }
            }
        }
    }

    if (line.used > 0 || line.truncated)
        controller_journal_flush_line(stream, &line);
    if (err_used > 0)
    {
        err_line[err_used] = '\0';
        controller_journal_set_diagnostic(stream, err_line, -1);
    }
    if (out_open) close(output[0]);
    if (err_open) close(errors[0]);
    controller_journal_reap(child, atomic_load(&stream->stop));
    if (!atomic_load(&stream->stop))
    {
        pthread_mutex_lock(&stream->mutex);
        stream->online = 0;
        if (strcmp(stream->diagnostic, "Following systemd journal") == 0)
            snprintf(stream->diagnostic, sizeof(stream->diagnostic),
                     "journalctl exited (inspect journal permissions and unit)");
        ++stream->version;
        pthread_mutex_unlock(&stream->mutex);
    }
    return NULL;
}

/* Starts exactly three independent journal readers. Returns 0 on success. */
static inline int controller_journal_start(ControllerJournalMonitor *monitor)
{
    memset(monitor, 0, sizeof(*monitor));
    int initialized = 0;
    for (int i = 0; i < CONTROLLER_JOURNAL_DEVICES; ++i)
    {
        ControllerJournalStream *s = &monitor->stream[i];
        if (pthread_mutex_init(&s->mutex, NULL) != 0)
            goto failure;
        ++initialized;
        atomic_init(&s->stop, 0);
        snprintf(s->diagnostic, sizeof(s->diagnostic), "Starting journal reader");
    }
    for (int i = 0; i < CONTROLLER_JOURNAL_DEVICES; ++i)
    {
        ControllerJournalWorkerArg *arg = malloc(sizeof(*arg));
        if (!arg) goto failure;
        arg->stream = &monitor->stream[i];
        arg->unit = controller_journal_units[i];
        if (pthread_create(&monitor->stream[i].thread, NULL,
                           controller_journal_worker, arg) != 0)
        {
            free(arg);
            goto failure;
        }
        monitor->stream[i].thread_started = 1;
    }
    return 0;

failure:
    for (int i = 0; i < initialized; ++i)
        atomic_store(&monitor->stream[i].stop, 1);
    for (int i = 0; i < initialized; ++i)
        if (monitor->stream[i].thread_started)
            (void)pthread_join(monitor->stream[i].thread, NULL);
    for (int i = 0; i < initialized; ++i)
        pthread_mutex_destroy(&monitor->stream[i].mutex);
    return -1;
}

/* Stop only the three follower children. Sensor services are NOT stopped. */
static inline void controller_journal_stop(ControllerJournalMonitor *monitor)
{
    for (int i = 0; i < CONTROLLER_JOURNAL_DEVICES; ++i)
        atomic_store(&monitor->stream[i].stop, 1);
    for (int i = 0; i < CONTROLLER_JOURNAL_DEVICES; ++i)
        if (monitor->stream[i].thread_started)
            (void)pthread_join(monitor->stream[i].thread, NULL);
    for (int i = 0; i < CONTROLLER_JOURNAL_DEVICES; ++i)
        pthread_mutex_destroy(&monitor->stream[i].mutex);
}

/* Thread-safe snapshot for Stage 5-C (chronological order).
 * newest_offset=0 means newest lines; larger values scroll into history.
 * 'out' must hold max_lines arrays of CONTROLLER_JOURNAL_LINE_LENGTH.
 * 'version', 'online' and 'diagnostic' are optional output pointers.
 */
static inline size_t controller_journal_snapshot(
    ControllerJournalMonitor *monitor, enum ControllerJournalDevice device,
    size_t newest_offset, char out[][CONTROLLER_JOURNAL_LINE_LENGTH],
    size_t max_lines, unsigned long *version, int *online,
    char *diagnostic, size_t diagnostic_capacity)
{
    if (!monitor || (int)device < 0 || (int)device >= CONTROLLER_JOURNAL_DEVICES ||
        (max_lines > 0 && !out))
        return 0;
    ControllerJournalStream *s = &monitor->stream[(int)device];
    pthread_mutex_lock(&s->mutex);
    if (version) *version = s->version;
    if (online) *online = s->online;
    if (diagnostic && diagnostic_capacity)
        snprintf(diagnostic, diagnostic_capacity, "%s", s->diagnostic);
    size_t count = 0;
    if (newest_offset < s->count)
    {
        count = s->count - newest_offset;
        if (count > max_lines) count = max_lines;
        size_t begin = (s->write_pos + CONTROLLER_JOURNAL_KEEP_LINES -
                        newest_offset - count) % CONTROLLER_JOURNAL_KEEP_LINES;
        for (size_t i = 0; i < count; ++i)
            memcpy(out[i], s->lines[(begin + i) % CONTROLLER_JOURNAL_KEEP_LINES],
                   CONTROLLER_JOURNAL_LINE_LENGTH);
    }
    pthread_mutex_unlock(&s->mutex);
    return count;
}

#endif /* MARINE_CONTROLLER_JOURNAL_H */
