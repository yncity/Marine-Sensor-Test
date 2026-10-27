#ifndef MARINE_CONTROLLER_STATUS_H
#define MARINE_CONTROLLER_STATUS_H

/* Stage 5-A: background status polling for the ncurses display.
 * Independent of controllerAsync command workers.
 * Does not access ncurses or alter device control commands.
 */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "controllerIpc.h"

#define CONTROLLER_STATUS_INTERVAL_SEC 3

typedef struct
{
    char sonar[128];
    char dvl[128];
    char servo[128];
} ControllerStatusView;

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    pthread_t thread;
    int stop;
    unsigned long version;
    ControllerStatusView view;
} ControllerStatusMonitor;

/* Search only complete lines, not substrings across devices. */
static int controller_status_field(const char *report,
                                   const char *prefix,
                                   char *out, size_t capacity)
{
    if (!report || !out || capacity == 0)
        return 0;

    size_t prefix_len = strlen(prefix);
    const char *line = report;
    while (*line)
    {
        const char *end = strchr(line, '\n');
        if (!end)
            end = line + strlen(line);

        if ((size_t)(end - line) >= prefix_len &&
            strncmp(line, prefix, prefix_len) == 0)
        {
            const char *value = line + prefix_len;
            size_t length = (size_t)(end - value);
            while (length > 0 && value[length - 1] == '\r')
                --length;
            if (length >= capacity)
                length = capacity - 1;
            memcpy(out, value, length);
            out[length] = '\0';
            return 1;
        }

        line = *end ? end + 1 : end;
    }
    return 0;
}

static void controller_status_device_line(const char *report,
                                          const char *device,
                                          char *out, size_t capacity)
{
    char service_key[48];
    char state_key[48];
    char service[40] = "UNKNOWN";
    char state[40] = "UNKNOWN";

    snprintf(service_key, sizeof(service_key), "%s SERVICE: ", device);
    snprintf(state_key, sizeof(state_key), "%s STATE: ", device);
    (void)controller_status_field(report, service_key, service, sizeof(service));
    (void)controller_status_field(report, state_key, state, sizeof(state));

    snprintf(out, capacity, "%s: service=%s | state=%s",
             device, service, state);
}

static void controller_status_format(ControllerStatusView *view,
                                     const char *report, int rc)
{
    if (rc != 0 || !report || strstr(report, "SONAR SERVICE:") == NULL)
    {
        snprintf(view->sonar, sizeof(view->sonar), "SONAR: status unavailable (controllerd/IPC)");
        snprintf(view->dvl, sizeof(view->dvl), "DVL: status unavailable (controllerd/IPC)");
        snprintf(view->servo, sizeof(view->servo), "SERVO: status unavailable (controllerd/IPC)");
        return;
    }

    controller_status_device_line(report, "SONAR", view->sonar, sizeof(view->sonar));
    controller_status_device_line(report, "DVL", view->dvl, sizeof(view->dvl));
    controller_status_device_line(report, "SERVO", view->servo, sizeof(view->servo));
}

static void *controller_status_worker(void *arg)
{
    ControllerStatusMonitor *monitor = (ControllerStatusMonitor *)arg;

    for (;;)
    {
        pthread_mutex_lock(&monitor->mutex);
        int stopping = monitor->stop;
        pthread_mutex_unlock(&monitor->mutex);
        if (stopping)
            break;

        char reply[4096];
        int rc = controller_ipc_request("status", reply, sizeof(reply));
        ControllerStatusView next;
        controller_status_format(&next, reply, rc);

        pthread_mutex_lock(&monitor->mutex);
        if (monitor->stop)
        {
            pthread_mutex_unlock(&monitor->mutex);
            break;
        }
        monitor->view = next;
        ++monitor->version;

        struct timespec deadline;
        if (clock_gettime(CLOCK_REALTIME, &deadline) == 0)
        {
            deadline.tv_sec += CONTROLLER_STATUS_INTERVAL_SEC;
            while (!monitor->stop)
            {
                int wait_rc = pthread_cond_timedwait(
                    &monitor->wake, &monitor->mutex, &deadline);
                if (wait_rc == ETIMEDOUT || wait_rc != 0)
                    break;
            }
        }
        pthread_mutex_unlock(&monitor->mutex);
    }
    return NULL;
}

static int controller_status_start(ControllerStatusMonitor *monitor)
{
    memset(monitor, 0, sizeof(*monitor));
    snprintf(monitor->view.sonar, sizeof(monitor->view.sonar), "SONAR: checking status...");
    snprintf(monitor->view.dvl, sizeof(monitor->view.dvl), "DVL: checking status...");
    snprintf(monitor->view.servo, sizeof(monitor->view.servo), "SERVO: checking status...");

    if (pthread_mutex_init(&monitor->mutex, NULL) != 0)
        return -1;
    if (pthread_cond_init(&monitor->wake, NULL) != 0)
    {
        pthread_mutex_destroy(&monitor->mutex);
        return -1;
    }
    if (pthread_create(&monitor->thread, NULL, controller_status_worker, monitor) != 0)
    {
        pthread_cond_destroy(&monitor->wake);
        pthread_mutex_destroy(&monitor->mutex);
        return -1;
    }
    return 0;
}

/* Returns 1 only when a new completed status snapshot is available. */
static int controller_status_snapshot(ControllerStatusMonitor *monitor,
                                      ControllerStatusView *out,
                                      unsigned long *seen_version)
{
    pthread_mutex_lock(&monitor->mutex);
    int updated = (monitor->version != *seen_version);
    if (updated)
    {
        *out = monitor->view;
        *seen_version = monitor->version;
    }
    pthread_mutex_unlock(&monitor->mutex);
    return updated;
}

static void controller_status_stop(ControllerStatusMonitor *monitor)
{
    pthread_mutex_lock(&monitor->mutex);
    monitor->stop = 1;
    pthread_cond_signal(&monitor->wake);
    pthread_mutex_unlock(&monitor->mutex);
    (void)pthread_join(monitor->thread, NULL);
    pthread_cond_destroy(&monitor->wake);
    pthread_mutex_destroy(&monitor->mutex);
}

#endif /* MARINE_CONTROLLER_STATUS_H */
