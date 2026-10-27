#ifndef MARINE_CONTROLLER_ASYNC_H
#define MARINE_CONTROLLER_ASYNC_H

/*
 * Stage 4-D-2
 *
 * - Each accepted command starts an independent IPC request.
 * - No global IPC BUSY.
 * - No command queue, deduplication, or automatic retry.
 * - Worker threads never access ncurses.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controllerIpc.h"

#define CONTROLLER_ASYNC_MAX_ACTIVE 16

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t idle;

    unsigned active;
    int stopping;

    int error_pending;
    unsigned overwritten_errors;
    char error[256];

    int notice_pending;
    char notice[256];

} ControllerAsync;

typedef struct
{
    ControllerAsync *state;
    char command[256];

} ControllerAsyncRequest;

// Determine whether an IPC reply reports an error.
static int controller_async_is_error(
    int rc,
    const char *reply)
{
    if (rc != 0)
        return 1;

    return strncmp(reply, "ERROR", 5) == 0 ||
           strncmp(reply, "SONAR_OFFLINE", 13) == 0 ||
           strncmp(reply, "DVL_OFFLINE", 11) == 0 ||
           strncmp(reply, "SERVO_OFFLINE", 13) == 0 ||
           strncmp(reply, "UNKNOWN_COMMAND", 15) == 0 ||
           strncmp(reply, "DENIED", 6) == 0 ||
           strstr(reply, "STOP_FAILED") != NULL ||
           strstr(reply, "STOP_INCOMPLETE") != NULL;
}

// Independent IPC request worker.
static void *controller_async_worker(void *arg)
{
    ControllerAsyncRequest *request =
        (ControllerAsyncRequest *)arg;

    ControllerAsync *state = request->state;

    const int is_stop =
        strcmp(request->command, "q") == 0;

    char reply[4096] = {0};

    // q waits until previously accepted IPC requests finish.
    if (is_stop)
    {
        pthread_mutex_lock(&state->mutex);

        while (state->active > 1)
        {
            pthread_cond_wait(
                &state->idle,
                &state->mutex);
        }

        pthread_mutex_unlock(&state->mutex);
    }

    // Actual IPC request
    int rc = controller_ipc_request(
        request->command,
        reply,
        sizeof(reply));

    int error = controller_async_is_error(rc, reply);

    pthread_mutex_lock(&state->mutex);

    if (error)
    {
        if (state->error_pending)
            ++state->overwritten_errors;

        size_t len = strcspn(reply, "\r\n");

        if (len > 170)
            len = 170;

        snprintf(
            state->error,
            sizeof(state->error),
            "%.50s: %.*s",
            request->command,
            (int)len,
            reply);

        state->error_pending = 1;
    }
    else if (is_stop)
    {
        const char *label =
            strstr(reply, "ALL_DEVICES_STOPPED")
                ? "SYSTEM: device stop confirmed by controllerd"
                : "SYSTEM: stop response received; verify status";

        snprintf(
            state->notice,
            sizeof(state->notice),
            "%s",
            label);

        state->notice_pending = 1;
    }

    if (is_stop)
        state->stopping = 0;

    --state->active;

    pthread_cond_broadcast(&state->idle);

    pthread_mutex_unlock(&state->mutex);

    free(request);

    return NULL;
}

static int controller_async_start(
    ControllerAsync *state)
{
    memset(state, 0, sizeof(*state));

    if (pthread_mutex_init(
            &state->mutex, NULL) != 0)
        return -1;

    if (pthread_cond_init(
            &state->idle, NULL) != 0)
    {
        pthread_mutex_destroy(&state->mutex);
        return -1;
    }

    return 0;
}

// 0: accepted
// -1: invalid command
// -2: resource limit or thread creation failure
// -3: global stop in progress
static int controller_async_submit(
    ControllerAsync *state,
    const char *command)
{
    if (!command ||
        !command[0] ||
        strlen(command) > 254 ||
        strchr(command, '\n') ||
        strchr(command, '\r'))
    {
        return -1;
    }

    const int is_stop =
        strcmp(command, "q") == 0;

    ControllerAsyncRequest *request =
        malloc(sizeof(*request));

    if (!request)
        return -2;

    request->state = state;

    snprintf(
        request->command,
        sizeof(request->command),
        "%s",
        command);

    pthread_mutex_lock(&state->mutex);

    if (state->stopping)
    {
        pthread_mutex_unlock(&state->mutex);
        free(request);
        return -3;
    }

    // Reserve one additional slot for the stop request.
    if (state->active >=
        CONTROLLER_ASYNC_MAX_ACTIVE + (unsigned)is_stop)
    {
        pthread_mutex_unlock(&state->mutex);
        free(request);
        return -2;
    }

    if (is_stop)
        state->stopping = 1;

    ++state->active;

    pthread_mutex_unlock(&state->mutex);

    pthread_t thread;

    if (pthread_create(
            &thread,
            NULL,
            controller_async_worker,
            request) != 0)
    {
        pthread_mutex_lock(&state->mutex);

        --state->active;

        if (is_stop)
            state->stopping = 0;

        pthread_cond_broadcast(&state->idle);

        pthread_mutex_unlock(&state->mutex);

        free(request);

        return -2;
    }

    (void)pthread_detach(thread);

    return 0;
}

// Read an IPC error without blocking ncurses.
static int controller_async_take_error(
    ControllerAsync *state,
    char *message,
    size_t capacity)
{
    if (!message || capacity == 0)
        return 0;

    pthread_mutex_lock(&state->mutex);

    int found = state->error_pending;

    if (found)
    {
        if (state->overwritten_errors)
        {
            snprintf(
                message,
                capacity,
                "IPC ERROR: %.170s (%u earlier errors overwritten)",
                state->error,
                state->overwritten_errors);
        }
        else
        {
            snprintf(
                message,
                capacity,
                "IPC ERROR: %.200s",
                state->error);
        }

        state->error_pending = 0;
        state->overwritten_errors = 0;
    }

    pthread_mutex_unlock(&state->mutex);

    return found;
}

// Count IPC requests still running.
static unsigned controller_async_active(
    ControllerAsync *state)
{
    pthread_mutex_lock(&state->mutex);

    unsigned active = state->active;

    pthread_mutex_unlock(&state->mutex);

    return active;
}

static int controller_async_take_notice(
    ControllerAsync *state,
    char *message,
    size_t capacity)
{
    if (!message || capacity == 0)
        return 0;

    pthread_mutex_lock(&state->mutex);

    int found = state->notice_pending;

    if (found)
    {
        snprintf(
            message,
            capacity,
            "%s",
            state->notice);

        state->notice_pending = 0;
    }

    pthread_mutex_unlock(&state->mutex);

    return found;
}

static int controller_async_stopping(
    ControllerAsync *state)
{
    pthread_mutex_lock(&state->mutex);

    int stopping = state->stopping;

    pthread_mutex_unlock(&state->mutex);

    return stopping;
}

#endif