#ifndef MARINE_CONTROLLER_ASYNC_H
#define MARINE_CONTROLLER_ASYNC_H

/* 4-D-1: single background IPC worker; ncurses stays on the main thread.
 * At most one request is outstanding. Further commands are rejected while busy.
 * Worker never calls ncurses; the UI polls the finished reply.
 * The worker is detached and lives until the TUI process exits.
 */

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "controllerIpc.h"

#define CONTROLLER_ASYNC_COMMAND_CAPACITY 256
#define CONTROLLER_ASYNC_REPLY_CAPACITY 4096

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    pthread_t thread;
    int pending;
    int busy;
    int finished;
    int result_code;
    char queued_command[CONTROLLER_ASYNC_COMMAND_CAPACITY];
    char completed_command[CONTROLLER_ASYNC_COMMAND_CAPACITY];
    char completed_reply[CONTROLLER_ASYNC_REPLY_CAPACITY];
} ControllerAsync;

static void *controller_async_worker(void *argument)
{
    ControllerAsync *state = (ControllerAsync *)argument;

    for (;;) {
        char command[CONTROLLER_ASYNC_COMMAND_CAPACITY];
        char reply[CONTROLLER_ASYNC_REPLY_CAPACITY] = {0};

        pthread_mutex_lock(&state->mutex);
        while (!state->pending)
            pthread_cond_wait(&state->wake, &state->mutex);

        memcpy(command, state->queued_command, sizeof(command));
        state->pending = 0;
        pthread_mutex_unlock(&state->mutex);

        int rc = controller_ipc_request(command, reply, sizeof(reply));

        pthread_mutex_lock(&state->mutex);
        memcpy(state->completed_command, command, sizeof(command));
        memcpy(state->completed_reply, reply, sizeof(reply));
        state->result_code = rc;
        state->finished = 1;
        state->busy = 0;
        pthread_mutex_unlock(&state->mutex);
    }
    return NULL;
}

static int controller_async_start(ControllerAsync *state)
{
    memset(state, 0, sizeof(*state));
    if (pthread_mutex_init(&state->mutex, NULL) != 0)
        return -1;
    if (pthread_cond_init(&state->wake, NULL) != 0) {
        pthread_mutex_destroy(&state->mutex);
        return -1;
    }
    if (pthread_create(&state->thread, NULL,
                       controller_async_worker, state) != 0) {
        pthread_cond_destroy(&state->wake);
        pthread_mutex_destroy(&state->mutex);
        return -1;
    }
    /* Do not join on exit: a slow IPC request must not freeze TUI shutdown. */
    (void)pthread_detach(state->thread);
    return 0;
}

/* Returns 0: accepted; 1: busy/not sent; -1: invalid/not sent. */
static int controller_async_submit(ControllerAsync *state, const char *command)
{
    if (!command || command[0] == '\0' || strlen(command) > 254 ||
        strchr(command, '\n') || strchr(command, '\r'))
        return -1;

    pthread_mutex_lock(&state->mutex);
    if (state->busy || state->finished) {
        pthread_mutex_unlock(&state->mutex);
        return 1;
    }
    snprintf(state->queued_command, sizeof(state->queued_command), "%s", command);
    state->busy = 1;
    state->pending = 1;
    state->finished = 0;
    pthread_cond_signal(&state->wake);
    pthread_mutex_unlock(&state->mutex);
    return 0;
}

/* Nonblocking. Returns 1 on a newly completed request; otherwise 0. */
static int controller_async_take(ControllerAsync *state,
                                 char *command, size_t command_cap,
                                 char *reply, size_t reply_cap,
                                 int *result_code)
{
    if (!command || !command_cap || !reply || !reply_cap || !result_code)
        return 0;

    pthread_mutex_lock(&state->mutex);
    if (!state->finished) {
        pthread_mutex_unlock(&state->mutex);
        return 0;
    }
    snprintf(command, command_cap, "%s", state->completed_command);
    snprintf(reply, reply_cap, "%s", state->completed_reply);
    *result_code = state->result_code;
    state->finished = 0;
    pthread_mutex_unlock(&state->mutex);
    return 1;
}

static int controller_async_busy(ControllerAsync *state)
{
    pthread_mutex_lock(&state->mutex);
    int busy = state->busy;
    pthread_mutex_unlock(&state->mutex);
    return busy;
}

#endif /* MARINE_CONTROLLER_ASYNC_H */
