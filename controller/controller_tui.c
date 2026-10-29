#define _POSIX_C_SOURCE 200809L

#include <ncurses.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "controllerAsync.h"
#include "controllerStatus.h"
#include "controllerJournal.h"
#include "controllerLogView.h"

#define COMMAND_CAPACITY 256
#define MESSAGE_CAPACITY 320
#define MIN_SCREEN_WIDTH 72
#define MIN_SCREEN_HEIGHT 18

static volatile sig_atomic_t exit_requested = 0;
static ControllerStatusView g_status_view = {
    "SONAR: checking status...",
    "DVL: checking status...",
    "SERVO: checking status...",
    "SYSTEM STATE: checking..."};
static ControllerJournalMonitor *g_journal_monitor = NULL;
static ControllerLogViewState g_log_view = {0};

static void on_exit_signal(int signo)
{
    (void)signo;
    exit_requested = 1;
}

/* Write within screen boundaries without relying on color support. */
static void draw_text(int y, int x, const char *text, int max_width)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    if (!text || y < 0 || y >= rows || x < 0 || x >= cols || max_width <= 0)
        return;
    int available = cols - x;
    if (available > max_width)
        available = max_width;
    if (available > 0)
        mvaddnstr(y, x, text, available);
}

static void draw_rule(int y)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    if (y >= 0 && y < rows && cols > 0)
        mvhline(y, 0, '-', cols);
}

static void draw_vertical_rule(int x, int first_row, int last_row)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    if (x < 0 || x >= cols)
        return;
    if (first_row < 0)
        first_row = 0;
    if (last_row >= rows)
        last_row = rows - 1;
    if (last_row >= first_row)
        mvvline(first_row, x, ACS_VLINE, last_row - first_row + 1);
}

static void draw_screen(const char *command, const char *message)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    erase();

    if (rows < MIN_SCREEN_HEIGHT || cols < MIN_SCREEN_WIDTH)
    {
        draw_text(0, 0, "MARINE CONTROLLER - STAGE 4-A", cols);
        if (rows > 2)
            draw_text(2, 0, "Resize terminal to at least 72 x 18.", cols);
        if (rows > 3)
            draw_text(3, 0, "F10 or Ctrl+X: exit the UI only.", cols);
        refresh();
        return;
    }

    const int split1 = cols / 3;
    const int split2 = (cols * 2) / 3;
    const int log_top = 8;
    const int log_bottom = rows - 6;
    const int input_row = rows - 2;
    const char *prompt = "COMMAND > ";
    const int prompt_width = (int)strlen(prompt);

    attron(A_BOLD);
    draw_text(0, 2, "MARINE SENSOR CONTROLLER", cols - 4);
    attroff(A_BOLD);
    draw_text(1, 2, g_status_view.system, cols - 4);
    draw_rule(2);

    draw_text(3, 2, g_status_view.sonar, cols - 4);
    draw_text(4, 2, g_status_view.dvl, cols - 4);
    draw_text(5, 2, g_status_view.servo, cols - 4);
    draw_rule(6);

    char title[96];

    attron(A_BOLD);

    snprintf(
        title, sizeof(title),
        "%c SONAR OUTPUT %s",
        g_log_view.selected == 0 ? '>' : ' ',
        g_log_view.offset[0] ? "[HISTORY]" : "[LIVE]");

    draw_text(7, 2, title, split1 - 3);

    snprintf(
        title, sizeof(title),
        "%c DVL OUTPUT %s",
        g_log_view.selected == 1 ? '>' : ' ',
        g_log_view.offset[1] ? "[HISTORY]" : "[LIVE]");

    draw_text(7, split1 + 2, title,
              split2 - split1 - 3);

    snprintf(
        title, sizeof(title),
        "%c SERVO OUTPUT %s",
        g_log_view.selected == 2 ? '>' : ' ',
        g_log_view.offset[2] ? "[HISTORY]" : "[LIVE]");

    draw_text(7, split2 + 2, title,
              cols - split2 - 3);

    attroff(A_BOLD);
    draw_vertical_rule(split1, 7, log_bottom);
    draw_vertical_rule(split2, 7, log_bottom);
    // Stage 5-C: show actual service journal messages.
    controller_log_view_draw(
        g_journal_monitor,
        CONTROLLER_JOURNAL_SONAR,
        log_top, log_bottom,
        2, split1 - 3,
        g_log_view.offset[CONTROLLER_JOURNAL_SONAR]);

    controller_log_view_draw(
        g_journal_monitor,
        CONTROLLER_JOURNAL_DVL,
        log_top, log_bottom,
        split1 + 2, split2 - split1 - 3,
        g_log_view.offset[CONTROLLER_JOURNAL_DVL]);

    controller_log_view_draw(
        g_journal_monitor,
        CONTROLLER_JOURNAL_SERVO,
        log_top, log_bottom,
        split2 + 2, cols - split2 - 3,
        g_log_view.offset[CONTROLLER_JOURNAL_SERVO]);

    draw_rule(rows - 5);
    draw_text(rows - 4, 2, "MESSAGE: ", cols - 4);
    draw_text(rows - 4, 11, message, cols - 13);
    draw_rule(rows - 3);

    draw_text(input_row, 2, prompt, prompt_width);
    const int input_x = 2 + prompt_width;
    const int input_width = cols - input_x - 2;
    size_t length = strlen(command);
    const char *visible = command;
    if (length > (size_t)input_width)
        visible = command + (length - (size_t)input_width);
    draw_text(input_row, input_x, visible, input_width);

    draw_text(
        rows - 1, 2,
        "Tab: pane | PgUp/PgDn: scroll | Home/End: history/live | F10/Ctrl+X: exit",
        cols - 4);

    size_t visible_len = strlen(visible);
    if (visible_len > (size_t)input_width)
        visible_len = (size_t)input_width;
    move(input_row, input_x + (int)visible_len);
    refresh();
}

int main(void)
{
    setlocale(LC_ALL, "");

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_exit_signal;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) != 0 ||
        sigaction(SIGTERM, &sa, NULL) != 0)
    {
        perror("sigaction");
        return 1;
    }

    if (initscr() == NULL)
    {
        fputs("ncurses: terminal initialization failed\n", stderr);
        return 1;
    }
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    timeout(100); /* Allows SIGTERM/Ctrl+C to be handled gracefully. */
    (void)curs_set(1);

    static ControllerAsync async_ipc;

    if (controller_async_start(&async_ipc) != 0)
    {
        endwin();
        fputs("Failed to start IPC worker thread\n", stderr);
        return 1;
    }

    static ControllerStatusMonitor status_monitor;

    int status_running =
        controller_status_start(&status_monitor) == 0;

    unsigned long status_version = 0;

    if (!status_running)
    {
        snprintf(g_status_view.sonar, sizeof(g_status_view.sonar),
                 "SONAR: status monitor unavailable");

        snprintf(g_status_view.dvl, sizeof(g_status_view.dvl),
                 "DVL: status monitor unavailable");

        snprintf(g_status_view.servo, sizeof(g_status_view.servo),
                 "SERVO: status monitor unavailable");
    }

    // Stage 5-B: collect device output from log files.
    static ControllerJournalMonitor journal_monitor;

    int journal_running =
        controller_journal_start(&journal_monitor) == 0;
    g_journal_monitor =
        journal_running ? &journal_monitor : NULL;

    char command[COMMAND_CAPACITY] = {0};
    size_t command_length = 0;
    char message[MESSAGE_CAPACITY] =
        "Stage 5-D ready: live logs and independent scrolling";

    draw_screen(command, message);
    int exit_confirmation = 0;

    while (!exit_requested)
    {
        int display_changed = 0;

        // Stage 5-A: Health status update
        if (status_running &&
            controller_status_snapshot(
                &status_monitor,
                &g_status_view,
                &status_version))
        {
            display_changed = 1;
        }

        // Stage 5-D: File log update and scroll tracking
        int log_rows = getmaxy(stdscr) - 13;

        if (log_rows < 1)
            log_rows = 1;

        if (journal_running &&
            controller_log_view_changed(
                &journal_monitor,
                &g_log_view,
                log_rows))
        {
            display_changed = 1;
        }

        // Redraw only when data has changed
        if (display_changed)
        {
            draw_screen(command, message);
        }

        char event_message[256];

        if (controller_async_take_error(
                &async_ipc,
                event_message,
                sizeof(event_message)) ||
            controller_async_take_notice(
                &async_ipc,
                event_message,
                sizeof(event_message)))
        {
            snprintf(
                message,
                sizeof(message),
                "%s",
                event_message);

            draw_screen(command, message);
        }

        int key = getch();

        if (key == ERR)
            continue;

        if (key == KEY_F(10) || key == 24)
        {
            unsigned active =
                controller_async_active(&async_ipc);

            if (active > 0 && !exit_confirmation)
            {
                exit_confirmation = 1;

                if (controller_async_stopping(&async_ipc))
                {
                    snprintf(
                        message,
                        sizeof(message),
                        "STOP request in progress: press again to exit (outcome may be unknown)");
                }
                else
                {
                    snprintf(
                        message,
                        sizeof(message),
                        "IPC active: press again to exit (unfinished requests may be interrupted)");
                }

                draw_screen(command, message);
                continue;
            }

            break;
        }

        exit_confirmation = 0;

        if (key == KEY_RESIZE)
        {
            int visible = getmaxy(stdscr) - 13;

            if (visible < 1)
                visible = 1;

            if (journal_running)
            {
                (void)controller_log_view_changed(
                    &journal_monitor,
                    &g_log_view,
                    visible);
            }

            draw_screen(command, message);
            continue;
        }

        if (controller_log_view_key(
                g_journal_monitor,
                &g_log_view,
                key,
                log_rows))
        {
            draw_screen(command, message);
            continue;
        }

        if (key == '\n' || key == '\r' || key == KEY_ENTER)
        {
            if (command_length == 0)
            {
                snprintf(
                    message,
                    sizeof(message),
                    "Empty command (nothing sent)");
            }
            else
            {
                int result = controller_async_submit(
                    &async_ipc,
                    command);

                if (result == 0)
                {
                    snprintf(
                        message,
                        sizeof(message),
                        "COMMAND: %.220s - request accepted",
                        command);

                    command[0] = '\0';
                    command_length = 0;
                }
                else if (result == -2)
                {
                    snprintf(
                        message,
                        sizeof(message),
                        "IPC LIMIT: command NOT sent");
                }
                else if (result == -3)
                {
                    snprintf(
                        message,
                        sizeof(message),
                        "SYSTEM: stop in progress - command NOT sent");
                }
                else
                {
                    snprintf(
                        message,
                        sizeof(message),
                        "Invalid command - not sent");
                }
            }
        }
        else if (key == KEY_BACKSPACE || key == 127 || key == 8)
        {
            if (command_length > 0)
                command[--command_length] = '\0';
        }
        else if (key == 21) /* Ctrl+U */
        {
            command[0] = '\0';
            command_length = 0;
        }
        else if (key >= 32 && key <= 126)
        {
            // Servo instant control: only when command input is empty
            if (command_length == 0 &&
                (key == 'u' || key == 'i' || key == 'o'))
            {
                char servo_command[2] = {
                    (char)key,
                    '\0'};

                int result = controller_async_submit(
                    &async_ipc,
                    servo_command);

                if (result == 0)
                {
                    int pulse =
                        key == 'u' ? 500 : key == 'i' ? 1520
                                                      : 2500;

                    snprintf(
                        message,
                        sizeof(message),
                        "SERVO: %d us setting requested",
                        pulse);
                }
                else if (result == -3)
                {
                    snprintf(
                        message,
                        sizeof(message),
                        "SERVO: stop in progress - command NOT sent");
                }
                else
                {
                    snprintf(
                        message,
                        sizeof(message),
                        "SERVO: command NOT sent");
                }
            }
            else if (command_length + 1 < sizeof(command))
            {
                // Ordinary command input
                command[command_length++] = (char)key;
                command[command_length] = '\0';
            }
            else
            {
                snprintf(
                    message,
                    sizeof(message),
                    "Command too long (max 255 bytes)");
            }
        }
        draw_screen(command, message);
    }
    unsigned unfinished = controller_async_active(&async_ipc);
    endwin();

    if (journal_running)
        controller_journal_stop(&journal_monitor);

    if (status_running)
        controller_status_stop(&status_monitor);

    puts("controller_tui exited; no device services were stopped.");

    if (unfinished)
    {
        puts("WARNING: an IPC request was still active; "
             "its final outcome is unknown.");
    }

    return 0;
}
