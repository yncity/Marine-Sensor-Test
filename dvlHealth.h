#ifndef DVL_HEALTH_H
#define DVL_HEALTH_H

/*
 * Observation-only DVL health tracking. Does not modify the DVL TCP stream
 * or its output frequency. A "frame" here is a newline-delimited,
 * object-shaped JSON candidate, not a fully validated JSON document.
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define DVL_HEALTH_TIMEOUT_NS (10LL * 1000000000LL)

typedef struct {
    int connected;
    int64_t connected_ns;
    int64_t last_byte_ns;
    int64_t last_frame_ns;
    uint64_t bytes_received;
    uint64_t frames_received;
    uint64_t velocity_frames;
    uint64_t malformed_lines;
    uint64_t oversized_lines;
} DvlHealth;

static inline int64_t dvl_health_now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return -1;
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static inline void dvl_health_init(DvlHealth *h)
{
    memset(h, 0, sizeof(*h));
    h->last_byte_ns = -1;
    h->last_frame_ns = -1;
}

static inline void dvl_health_on_connect(DvlHealth *h, int64_t now_ns)
{
    h->connected = 1;
    h->connected_ns = now_ns;
    h->last_byte_ns = -1;
    h->last_frame_ns = -1;
}

static inline void dvl_health_on_bytes(DvlHealth *h, size_t count, int64_t now_ns)
{
    if (count > 0) {
        h->bytes_received += (uint64_t)count;
        h->last_byte_ns = now_ns;
    }
}

/* The existing DVL reader stores one JSON object per newline. This check
 * only confirms framing/outer braces, not strict JSON syntax or data quality.
 */
static inline int dvl_health_is_object_line(const char *line)
{
    const unsigned char *begin = (const unsigned char *)line;
    while (*begin && isspace(*begin)) ++begin;
    if (*begin != '{') return 0;
    const unsigned char *end = begin + strlen((const char *)begin);
    while (end > begin && isspace(end[-1])) --end;
    return end > begin + 1 && end[-1] == '}';
}

static inline void dvl_health_on_line(DvlHealth *h, const char *line,
                               int64_t now_ns, int is_velocity)
{
    if (dvl_health_is_object_line(line)) {
        h->frames_received++;
        if (is_velocity) h->velocity_frames++;
        h->last_frame_ns = now_ns;
    } else {
        h->malformed_lines++;
    }
}

static inline void dvl_health_on_oversized(DvlHealth *h)
{
    h->oversized_lines++;
}

static inline void dvl_health_state_at(const DvlHealth *h, int64_t now_ns,
                                const char **state, const char **error)
{
    *error = "NONE";
    if (!h->connected) {
        *state = "STARTING";
    } else if (h->last_frame_ns >= 0 &&
               now_ns - h->last_frame_ns <= DVL_HEALTH_TIMEOUT_NS) {
        *state = "RUNNING";
    } else if (h->last_frame_ns < 0 &&
               now_ns - h->connected_ns < DVL_HEALTH_TIMEOUT_NS) {
        *state = "CONNECTED";
    } else if (h->last_byte_ns >= 0 &&
               now_ns - h->last_byte_ns <= DVL_HEALTH_TIMEOUT_NS) {
        *state = "DEGRADED";
        *error = "NO_COMPLETE_JSON_FRAME";
    } else {
        *state = "ERROR";
        *error = "DATA_TIMEOUT";
    }
}

static inline void dvl_health_report_at(const DvlHealth *h, int64_t now_ns,
                                 char *out, size_t capacity)
{
    const char *state, *error;
    dvl_health_state_at(h, now_ns, &state, &error);
    long long age_ms = h->last_frame_ns < 0 ? -1 :
        (long long)((now_ns - h->last_frame_ns) / 1000000LL);
    snprintf(out, capacity,
             "DVL STATE: %s\nERROR: %s\nTCP: %s\n"
             "LAST_FRAME_MS: %lld\nFRAMES: %llu\nVELOCITY_FRAMES: %llu\n"
             "MALFORMED_LINES: %llu\nOVERSIZED_LINES: %llu\n",
             state, error, h->connected ? "CONNECTED" : "NOT_CONNECTED",
             age_ms,
             (unsigned long long)h->frames_received,
             (unsigned long long)h->velocity_frames,
             (unsigned long long)h->malformed_lines,
             (unsigned long long)h->oversized_lines);
}

static inline void dvl_health_report(const DvlHealth *h, char *out, size_t capacity)
{
    int64_t now_ns = dvl_health_now_ns();
    if (now_ns < 0) {
        snprintf(out, capacity, "DVL STATE: ERROR\nERROR: CLOCK_FAILURE\n");
        return;
    }
    dvl_health_report_at(h, now_ns, out, capacity);
}

#endif
