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
    unsigned active;

    int error_pending;
    char error[256];

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

    char reply[4096] = {0};

    int rc = controller_ipc_request(
        request->command,
        reply,
        sizeof(reply));

    pthread_mutex_lock(&state->mutex);

    if (controller_async_is_error(rc, reply))
    {
        size_t length = strcspn(reply, "\r\n");

        if (length > 170)
            length = 170;

        snprintf(
            state->error,
            sizeof(state->error),
            "%.50s: %.*s",
            request->command,
            (int)length,
            reply);

        state->error_pending = 1;
    }

    --state->active;

    pthread_mutex_unlock(&state->mutex);

    free(request);

    return NULL;
}

// Initialize IPC state.
static int controller_async_start(
    ControllerAsync *state)
{
    memset(state, 0, sizeof(*state));

    return pthread_mutex_init(
               &state->mutex,
               NULL) == 0
               ? 0
               : -1;
}

// Submit one independent IPC command.
//  0: accepted
// -1: invalid command
// -2: resource limit or thread creation failed
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

    if (state->active >= CONTROLLER_ASYNC_MAX_ACTIVE)
    {
        pthread_mutex_unlock(&state->mutex);
        free(request);
        return -2;
    }

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
        snprintf(
            message,
            capacity,
            "IPC ERROR: %s",
            state->error);

        state->error_pending = 0;
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

#endif