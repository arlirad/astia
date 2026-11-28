#define _POSIX_C_SOURCE 200809L

#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int cu_tee_main(int argc, char** argv);

static int report_descriptor = -1;

static void note_signal(int signal_number) {
    (void) signal_number;

    if (report_descriptor >= 0)
        (void) write(report_descriptor, "s", 1);
}

static int fail(const char* what) {
    printf("tee EINTR test failed: %s\n", what);

    return 1;
}

static int sleep_milliseconds(long milliseconds) {
    struct timespec delay = {.tv_sec  = milliseconds / 1000,
                             .tv_nsec = (milliseconds % 1000) * 1000000L};

    return nanosleep(&delay, 0);
}

static pid_t spawn_tee(int input_read, int input_write, int output, int ready_write,
                       int report_write) {
    pid_t child = fork();
    if (child != 0)
        return child;

    report_descriptor = report_write;

    struct sigaction action = {0};

    action.sa_handler = note_signal;

    sigemptyset(&action.sa_mask);

    if (sigaction(SIGUSR1, &action, 0) != 0)
        _exit(127);

    if (dup2(input_read, 0) < 0 || dup2(output, 1) < 0)
        _exit(127);

    if (input_read != 0)
        close(input_read);

    if (input_write != 0 && input_write != 1)
        close(input_write);

    if (output != 0 && output != 1)
        close(output);

    if (ready_write >= 0) {
        char ready = 'r';

        if (write(ready_write, &ready, 1) != 1)
            _exit(127);

        if (ready_write != 0 && ready_write != 1)
            close(ready_write);
    }

    char* argv[] = {"tee", 0};

    _exit(cu_tee_main(1, argv));
}

static int file_equals(const char* path, const char* expected) {
    char buffer[256];
    int  descriptor = open(path, O_RDONLY);

    if (descriptor < 0)
        return 0;

    long length = read(descriptor, buffer, sizeof buffer - 1);

    close(descriptor);

    if (length < 0)
        return 0;

    buffer[length] = '\0';

    return strcmp(buffer, expected) == 0;
}

static int read_signal_reports(int descriptor) {
    char buffer[16];
    int  total = 0;

    for (;;) {
        long got = read(descriptor, buffer, sizeof buffer);

        if (got < 0 && errno == EINTR)
            continue;

        if (got <= 0)
            return total;

        total += (int) got;
    }
}

static int interrupted_read_is_retried(void) {
    static const char payload[] = "tee keeps interrupted input\n";

    int input[2];
    int ready[2];
    int reports[2];

    if (pipe(input) != 0 || pipe(ready) != 0 || pipe(reports) != 0)
        return fail("pipe");

    int output = open("/tmp/tee_eintr_output", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (output < 0)
        return fail("open output");

    pid_t child = spawn_tee(input[0], input[1], output, ready[1], reports[1]);

    close(input[0]);
    close(ready[1]);
    close(reports[1]);
    close(output);

    if (child < 0)
        return fail("fork");

    char ready_byte = 0;

    if (read(ready[0], &ready_byte, 1) != 1) {
        (void) kill(child, SIGKILL);
        (void) waitpid(child, 0, 0);

        return fail("child readiness");
    }

    close(ready[0]);

    if (sleep_milliseconds(50) != 0)
        return fail("sleep");

    for (int attempt = 0; attempt < 10; ++attempt) {
        if (kill(child, SIGUSR1) != 0 || sleep_milliseconds(20) != 0) {
            (void) kill(child, SIGKILL);
            (void) waitpid(child, 0, 0);

            return fail("interrupt");
        }
    }

    long written = write(input[1], payload, sizeof payload - 1);

    close(input[1]);

    int status = 0;

    if (waitpid(child, &status, 0) != child)
        return fail("waitpid");

    int interrupts = read_signal_reports(reports[0]);

    close(reports[0]);

    if (interrupts == 0)
        return fail("no signal interrupted the read");

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return fail("tee failed after EINTR");

    if (written != (long) (sizeof payload - 1))
        return fail("write input");

    if (!file_equals("/tmp/tee_eintr_output", payload))
        return fail("output truncated after EINTR");

    return 0;
}

static int read_error_is_fatal(void) {
    int input[2];

    if (pipe(input) != 0)
        return fail("pipe");

    int output = open("/dev/null", O_WRONLY);
    if (output < 0)
        return fail("open /dev/null");

    pid_t child = spawn_tee(input[1], input[0], output, -1, -1);

    close(input[0]);
    close(input[1]);
    close(output);

    if (child < 0)
        return fail("fork");

    int status = 0;

    if (waitpid(child, &status, 0) != child)
        return fail("waitpid");

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 1)
        return fail("read error was not fatal");

    return 0;
}

int main(void) {
    int status = 0;

    (void) signal(SIGPIPE, SIG_IGN);

    status |= interrupted_read_is_retried();
    status |= read_error_is_fatal();

    if (status != 0)
        return 1;

    printf("tee EINTR tests ok\n");

    return 0;
}
