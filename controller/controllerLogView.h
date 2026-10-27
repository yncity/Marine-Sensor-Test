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

/* Returns 1 if any of the three journal streams changed since last check.
 * Caller must pass a persistent array initialized to {0, 0, 0}.
 */
static inline int controller_log_view_changed(
    ControllerJournalMonitor *monitor,
    unsigned long seen[CONTROLLER_JOURNAL_DEVICES])
{
    if (!monitor || !seen)
        return 0;

    int changed = 0;
    for (int i = 0; i < CONTROLLER_JOURNAL_DEVICES; ++i)
    {
        unsigned long version = 0;
        (void)controller_journal_snapshot(
            monitor, (enum ControllerJournalDevice)i,
            0, NULL, 0, &version, NULL, NULL, 0);
        if (version != seen[i])
        {
            seen[i] = version;
            changed = 1;
        }
    }
    return changed;
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
    int first_row, int last_row, int x, int width)
{
    if (first_row > last_row || width <= 0)
        return;

    if (!monitor)
    {
        attron(A_DIM);
        mvaddnstr(first_row, x, "[journal reader unavailable]", width);
        attroff(A_DIM);
        return;
    }

    int online = 0;
    char diagnostic[CONTROLLER_JOURNAL_DIAGNOSTIC_LENGTH] = {0};
    (void)controller_journal_snapshot(
        monitor, device, 0, NULL, 0,
        NULL, &online, diagnostic, sizeof(diagnostic));

    int content_top = first_row;
    if (!online)
    {
        char notice[CONTROLLER_JOURNAL_DIAGNOSTIC_LENGTH + 12];
        snprintf(notice, sizeof(notice), "[journal: %s]", diagnostic);
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
        monitor, device, 0, lines, limit,
        NULL, NULL, NULL, 0);

    if (count == 0)
    {
        if (online)
        {
            attron(A_DIM);
            mvaddnstr(content_top, x, "[waiting for service logs]", width);
            attroff(A_DIM);
        }
        return;
    }

    /* Bottom-align the newest lines without wrapping across columns. */
    int first_print = last_row - (int)count + 1;
    for (size_t i = 0; i < count; ++i)
        mvaddnstr(first_print + (int)i, x, lines[i], width);
}

#endif /* MARINE_CONTROLLER_LOG_VIEW_H */
