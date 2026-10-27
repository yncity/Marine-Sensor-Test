#define _POSIX_C_SOURCE 200809L

#include <ncurses.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include "controllerIpc.h"

#define COMMAND_CAPACITY 256
#define MESSAGE_CAPACITY 320
#define MIN_SCREEN_WIDTH 72
#define MIN_SCREEN_HEIGHT 18

static volatile sig_atomic_t exit_requested = 0;

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
        mvhline(y, 0, ACS_HLINE, cols);
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
    draw_text(1, 2, "STAGE 4-C | ENTER: COMMANDS | INSTANT: SERVO u/i/o", cols - 4);
    draw_rule(2);

    draw_text(3, 2, "SONAR  : status not connected (Stage 5)", cols - 4);
    draw_text(4, 2, "DVL    : status not connected (Stage 5)", cols - 4);
    draw_text(5, 2, "SERVO  : status not connected (Stage 5)", cols - 4);
    draw_rule(6);

    attron(A_BOLD);
    draw_text(7, 2, "SONAR OUTPUT", split1 - 3);
    draw_text(7, split1 + 2, "DVL OUTPUT", split2 - split1 - 3);
    draw_text(7, split2 + 2, "SERVO OUTPUT", cols - split2 - 3);
    attroff(A_BOLD);
    draw_vertical_rule(split1, 7, log_bottom);
    draw_vertical_rule(split2, 7, log_bottom);
    draw_text(log_top + 1, 2, "Log connection: Stage 5", split1 - 3);
    draw_text(log_top + 1, split1 + 2, "Log connection: Stage 5", split2 - split1 - 3);
    draw_text(log_top + 1, split2 + 2, "Log connection: Stage 5", cols - split2 - 3);

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

    draw_text(rows - 1, 2,
              "u/i/o: Servo instant | Enter: command | Ctrl+U: clear | F10/Ctrl+X: exit",
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

    char command[COMMAND_CAPACITY] = {0};
    size_t command_length = 0;
    char message[MESSAGE_CAPACITY] =
        "Stage 4-C ready: u/i/o instant; other commands require Enter";

    draw_screen(command, message);
    while (!exit_requested)
    {
        int key = getch();
        if (key == ERR)
            continue;

        if (key == KEY_F(10) || key == 24) /* Ctrl+X */
            break;

        if (key == KEY_RESIZE)
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
                // 1. Send command to controllerd
                char reply[4096];

                int rc = controller_ipc_request(
                    command,
                    reply,
                    sizeof(reply));

                // 2. Convert multi-line response into one line
                char compact[240];
                size_t j = 0;

                for (size_t i = 0;
                     reply[i] != '\0' && j + 1 < sizeof(compact);
                     ++i)
                {
                    if (reply[i] == '\r')
                        continue;

                    if (reply[i] == '\n')
                    {
                        if (reply[i + 1] != '\0' &&
                            j + 3 < sizeof(compact))
                        {
                            compact[j++] = ' ';
                            compact[j++] = '|';
                            compact[j++] = ' ';
                        }
                    }
                    else if ((unsigned char)reply[i] >= 32)
                    {
                        compact[j++] = reply[i];
                    }
                }

                compact[j] = '\0';

                // 3. Display result in MESSAGE area
                snprintf(
                    message,
                    sizeof(message),
                    "%s %.40s -> %.235s",
                    rc == 0 ? "IPC" : "IPC FAILED",
                    command,
                    compact);
            }

            // 4. Clear command input
            command[0] = '\0';
            command_length = 0;
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

                char reply[512];

                // Send immediately without Enter
                int rc = controller_ipc_request(
                    servo_command,
                    reply,
                    sizeof(reply));

                // Display only the first response line
                reply[strcspn(reply, "\r\n")] = '\0';

                snprintf(
                    message,
                    sizeof(message),
                    "%s Servo %c -> %.200s",
                    rc == 0 ? "IPC" : "IPC FAILED",
                    (char)key,
                    reply);
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

    endwin();
    puts("controller_tui exited; no device services were stopped.");
    return 0;
}
