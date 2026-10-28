#ifndef MARINE_CONTROLLER_LOG_VIEW_H
#define MARINE_CONTROLLER_LOG_VIEW_H

/* Stage 5-C: render the journal messages collected by Stage 5-B.
 * Only the ncurses/main thread may call these functions.
 * Device messages come exclusively from controller_journal_snapshot().
 * No device commands are sent, and no background thread uses ncurses.
 * At Stage 5-C, newest_offset stays at zero (follow latest logs).
 */

#include <ncurses.h>
#include <stdio.h>
#include "controllerJournal.h"

typedef struct
{
    int selected;

    size_t offset[CONTROLLER_JOURNAL_DEVICES];

    unsigned long version[CONTROLLER_JOURNAL_DEVICES];

    unsigned long long seen_lines[CONTROLLER_JOURNAL_DEVICES];

} ControllerLogViewState;

static inline size_t controller_log_view_max_offset(
    size_t retained,
    int visible_rows)
{
    size_t page =
        visible_rows > 0 ? (size_t)visible_rows : 1;

    return retained > page ? retained - page : 0;
}
/* Returns 1 if any of the three journal streams changed since last check.
 * Caller must pass a persistent array initialized to {0, 0, 0}.
 */
static inline int controller_log_view_changed(
    ControllerJournalMonitor *monitor,
    ControllerLogViewState *view,
    int visible_rows)
{
    if (!monitor || !view)
        return 0;

    int changed = 0;

    for (int i = 0; i < CONTROLLER_JOURNAL_DEVICES; ++i)
    {
        unsigned long version = 0;
        unsigned long long total = 0;
        size_t retained = 0;

        controller_journal_stats(
            monitor,
            (enum ControllerJournalDevice)i,
            &version,
            &total,
            &retained,
            NULL);

        size_t max =
            controller_log_view_max_offset(
                retained, visible_rows);

        if (version != view->version[i])
        {
            if (view->offset[i] > 0 &&
                total > view->seen_lines[i])
            {
                unsigned long long added =
                    total - view->seen_lines[i];

                size_t current =
                    view->offset[i] > max
                        ? max
                        : view->offset[i];

                size_t room = max - current;

                view->offset[i] =
                    added >= (unsigned long long)room
                        ? max
                        : current + (size_t)added;
            }

            view->version[i] = version;
            changed = 1;
        }

        view->seen_lines[i] = total;

        if (view->offset[i] > max)
        {
            view->offset[i] = max;
            changed = 1;
        }
    }

    return changed;
}

static inline int controller_log_view_key(
    ControllerJournalMonitor *monitor,
    ControllerLogViewState *view,
    int key,
    int visible_rows)
{
    if (!view)
        return 0;

    // Select next pane.
    if (key == '\t')
    {
        view->selected =
            (view->selected + 1) %
            CONTROLLER_JOURNAL_DEVICES;

        return 1;
    }

    // Ignore ordinary command keys.
    if (key != KEY_PPAGE &&
        key != KEY_NPAGE &&
        key != KEY_HOME &&
        key != KEY_END)
    {
        return 0;
    }

    if (!monitor)
        return 1;

    const int i = view->selected;

    size_t retained = 0;
    unsigned long version = 0;
    unsigned long long total = 0;

    controller_journal_stats(
        monitor,
        (enum ControllerJournalDevice)i,
        &version,
        &total,
        &retained,
        NULL);

    size_t max =
        controller_log_view_max_offset(
            retained, visible_rows);

    size_t page =
        visible_rows > 1
            ? (size_t)(visible_rows - 1)
            : 1;

    size_t current =
        view->offset[i] > max
            ? max
            : view->offset[i];

    if (key == KEY_END)
    {
        view->offset[i] = 0;
    }
    else if (key == KEY_HOME)
    {
        view->offset[i] = max;
    }
    else if (key == KEY_PPAGE)
    {
        view->offset[i] =
            page >= max - current
                ? max
                : current + page;
    }
    else if (key == KEY_NPAGE)
    {
        view->offset[i] =
            current > page
                ? current - page
                : 0;
    }

    view->version[i] = version;
    view->seen_lines[i] = total;

    return 1;
}

/* Draw one device's live log lines, inside its existing pane boundary.
 * Only complete messages from the journal ring are drawn as log lines.
 * A dim [journal: ...] row is a reader diagnostic, never device output.
 * Calls are serialized by the ncurses main thread; the static scratch
 * buffer is deliberately not used from background threads.
 */
static inline void controller_log_view_draw(
    ControllerJournalMonitor *monitor,
    enum ControllerJournalDevice device,
    int first_row,
    int last_row,
    int x,
    int width,
    size_t newest_offset)
{
    if (first_row > last_row || width <= 0)
        return;

    if (!monitor)
    {
        attron(A_DIM);
        mvaddnstr(first_row, x, "[log reader unavailable]", width);
        attroff(A_DIM);
        return;
    }

    int online = 0;
    char diagnostic[CONTROLLER_JOURNAL_DIAGNOSTIC_LENGTH] = {0};
    (void)controller_journal_snapshot(
        monitor, device, 0, NULL, 0,
        NULL, &online, diagnostic, sizeof(diagnostic));

    int content_top = first_row;
    if (!online ||
        (diagnostic[0] &&
         strcmp(diagnostic, "Following file logs") != 0))
    {
        char notice[CONTROLLER_JOURNAL_DIAGNOSTIC_LENGTH + 12];
        snprintf(notice, sizeof(notice), "[file: %s]", diagnostic);
        attron(A_DIM);
        mvaddnstr(content_top, x, notice, width);
        attroff(A_DIM);
        ++content_top;
    }

    if (content_top > last_row)
        return;

    int available = last_row - content_top + 1;
    size_t limit = (size_t)available;
    if (limit > CONTROLLER_JOURNAL_KEEP_LINES)
        limit = CONTROLLER_JOURNAL_KEEP_LINES;

    /* 160 KiB of reusable storage in static/BSS, not the Pi's small stack. */
    static char lines[CONTROLLER_JOURNAL_KEEP_LINES]
                     [CONTROLLER_JOURNAL_LINE_LENGTH];
    size_t count = controller_journal_snapshot(
        monitor, device, newest_offset, lines, limit,
        NULL, NULL, NULL, 0);

    if (online && count == 0)
    {
        attron(A_DIM);
        mvaddnstr(content_top, x,
                  "[waiting for file logs]", width);
        attroff(A_DIM);
    }

    /* Bottom-align the newest lines without wrapping across columns. */
    int first_print = last_row - (int)count + 1;
    for (size_t i = 0; i < count; ++i)
        mvaddnstr(first_print + (int)i, x, lines[i], width);
}

#endif /* MARINE_CONTROLLER_LOG_VIEW_H */
