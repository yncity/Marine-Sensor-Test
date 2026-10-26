#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_PWMCHIP "/sys/class/pwm/pwmchip0"
#define PWM_CHANNEL 0
#define PWM_PERIOD_NS 3003003L /* approximately 333 Hz */
#define MIN_PULSE_US 500
#define NEUTRAL_PULSE_US 1520
#define MAX_PULSE_US 2500

static volatile sig_atomic_t running = 1;
static struct termios old_tty;
static int tty_changed = 0;

static void on_signal(int signo)
{
    (void)signo;
    running = 0;
}

static int write_number(const char *path, long value)
{
    char buffer[40];
    int length = snprintf(buffer, sizeof(buffer), "%ld", value);
    int fd = open(path, O_WRONLY);
    if (fd < 0)
    {
        perror(path);
        return -1;
    }

    ssize_t sent = write(fd, buffer, (size_t)length);
    int write_error = errno;
    close(fd);
    if (sent != length)
    {
        errno = sent < 0 ? write_error : EIO;
        perror(path);
        return -1;
    }
    return 0;
}

static int set_pulse(const char *duty_path, int pulse_us)
{
    if (pulse_us < MIN_PULSE_US || pulse_us > MAX_PULSE_US)
        return -1;

    if (write_number(duty_path, pulse_us * 1000L) != 0)
        return -1;

    printf("\nPWM: 333 Hz, pulse width: %d us\n", pulse_us);
    fflush(stdout);
    return 0;
}

static int setup_terminal(void)
{
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &old_tty) < 0)
    {
        fprintf(stderr, "Run this program in an interactive terminal.\n");
        return -1;
    }
    struct termios mode = old_tty;
    mode.c_lflag &= (tcflag_t) ~(ICANON | ECHO); /* Ctrl-C still works */
    mode.c_cc[VMIN] = 1;
    mode.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &mode) < 0)
    {
        perror("tcsetattr");
        return -1;
    }
    tty_changed = 1;
    return 0;
}

int main(int argc, char **argv)
{
    /* Keep legacy interactive CLI and add terminal-free service mode. */
    int service_mode = (argc > 1 && strcmp(argv[1], "--service") == 0);
    int arg_index = service_mode ? 2 : 1;

    if (argc > arg_index + 1)
    {
        fprintf(stderr, "Usage: %s [--service] [PWMCHIP_PATH]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *pwmchip = (argc > arg_index) ? argv[arg_index] : DEFAULT_PWMCHIP;
    char export_path[128];
    char pwm_dir[128];
    char enable_path[160];
    char period_path[160];
    char duty_path[160];
    int result = EXIT_FAILURE;
    int exported_here = 0;

    snprintf(export_path, sizeof(export_path), "%s/export", pwmchip);
    snprintf(pwm_dir, sizeof(pwm_dir), "%s/pwm%d", pwmchip, PWM_CHANNEL);
    snprintf(enable_path, sizeof(enable_path), "%s/enable", pwm_dir);
    snprintf(period_path, sizeof(period_path), "%s/period", pwm_dir);
    snprintf(duty_path, sizeof(duty_path), "%s/duty_cycle", pwm_dir);

    if (access(pwmchip, F_OK) != 0)
    {
        fprintf(stderr, "Cannot find %s. Enable PWM overlay and check pwmchip number.\n", pwmchip);
        return EXIT_FAILURE;
    }
    if (access(pwm_dir, F_OK) != 0)
    {
        if (write_number(export_path, PWM_CHANNEL) != 0)
            return EXIT_FAILURE;
        exported_here = 1;
        for (int count = 0; count < 50 && access(pwm_dir, F_OK) != 0; ++count)
        {
            struct timespec delay = {.tv_sec = 0, .tv_nsec = 10000000L};
            nanosleep(&delay, NULL);
        }
    }
    if (access(pwm_dir, F_OK) != 0)
    {
        fprintf(stderr, "PWM channel was not exported.\n");
        goto cleanup;
    }

    /* Do not require or reconfigure a terminal in service mode. */
    if (!service_mode && setup_terminal() != 0)
        goto cleanup;

    /* Configure with output disabled. Preserve existing PWM behavior. */
    if (write_number(enable_path, 0) != 0 ||
        write_number(duty_path, 0) != 0 ||
        write_number(period_path, PWM_PERIOD_NS) != 0 ||
        write_number(duty_path, NEUTRAL_PULSE_US * 1000L) != 0 ||
        write_number(enable_path, 1) != 0)
        goto cleanup;

    struct sigaction sa = {0};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) != 0 ||
        sigaction(SIGTERM, &sa, NULL) != 0)
    {
        perror("sigaction");
        goto cleanup;
    }

    puts("AGFRC A35BHLP / hardware PWM test");
    puts("GPIO18 (physical pin 12), 333 Hz");
    puts("Starting at neutral (1520 us). Do not use end positions before checking clearance.");
    if (service_mode)
    {
        puts("Service mode: PWM initialized; awaiting control integration.");
        puts("Stop with SIGTERM or SIGINT. IPC commands will be added in stage 3.");
    }
    else
    {
        puts("u = MIN 500 us | i = NEUTRAL 1520 us | o = MAX 2500 us | q = QUIT");
    }
    fflush(stdout);

    result = EXIT_SUCCESS;
    if (service_mode)
    {
        /* Keep running without stdin; wake periodically to check stop flag. */
        while (running)
        {
            const struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000L};
            nanosleep(&delay, NULL);
        }
    }
    else
    {
        while (running)
        {
            char key;
            ssize_t length = read(STDIN_FILENO, &key, 1);
            if (length < 0 && errno == EINTR)
                continue;
            if (length != 1)
                break;
            int pulse = 0;
            switch (key)
            {
            case 'u':
            case 'U':
                pulse = MIN_PULSE_US;
                break;
            case 'i':
            case 'I':
                pulse = NEUTRAL_PULSE_US;
                break;
            case 'o':
            case 'O':
                pulse = MAX_PULSE_US;
                break;
            case 'q':
            case 'Q':
                running = 0;
                continue;
            default:
                continue;
            }
            if (set_pulse(duty_path, pulse) != 0)
            {
                result = EXIT_FAILURE;
                break;
            }
        }
    }

cleanup:
    /* Stopping PWM also releases servo holding force. */
    (void)write_number(enable_path, 0);
    if (exported_here)
    {
        char unexport_path[128];
        snprintf(unexport_path, sizeof(unexport_path), "%s/unexport", pwmchip);
        (void)write_number(unexport_path, PWM_CHANNEL);
    }
    if (tty_changed)
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &old_tty);
    putchar('\n');
    puts(service_mode ? "PWM stopped; service exiting." : "PWM stopped; terminal restored.");
    return result;
}
