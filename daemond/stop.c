#include "stop.h"

#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

enum { STOP_WAIT_SLICE_MS = 10 };

static long long monotonic_ms(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return -1;

    return (long long) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void snooze_ms(int milliseconds) {
    struct timespec delay;

    delay.tv_sec  = milliseconds / 1000;
    delay.tv_nsec = (long) (milliseconds % 1000) * 1000000L;

    (void) nanosleep(&delay, 0);
}

static int deliver(struct stop_process* process, int signal_number) {
    int result;

    if (process->pgid >= 0) {
        result = kill(-process->pgid, signal_number);

        /* The group is created just after spawn; fall back to the process
         * itself when the child has not formed it yet. */
        if (result < 0 && errno == ESRCH && process->pid > 0)
            result = kill(process->pid, signal_number);
    }
    else {
        result = kill(process->pid, signal_number);
    }

    return result < 0 && errno != ESRCH ? errno : 0;
}

static int collect(struct stop_process* processes, int count) {
    int remaining = 0;

    for (int i = 0; i < count; ++i) {
        struct stop_process* process = &processes[i];

        int status;

        pid_t result;

        if (process->pid < 0 || process->exited)
            continue;

        result = waitpid(process->pid, &status, WNOHANG);

        if (result == process->pid) {
            process->status = status;
            process->exited = 1;

            continue;
        }

        ++remaining;
    }

    return remaining;
}

static int wait_for_exit(struct stop_process* processes, int count, int timeout_ms) {
    long long deadline = monotonic_ms();
    if (deadline < 0)
        return -1;

    deadline += timeout_ms;

    for (;;) {
        long long now, left;
        int       remaining = collect(processes, count);

        if (!remaining)
            return 0;

        now = monotonic_ms();
        if (now < 0)
            return remaining;

        left = deadline - now;
        if (left <= 0)
            return remaining;

        snooze_ms(left < STOP_WAIT_SLICE_MS ? (int) left : STOP_WAIT_SLICE_MS);
    }
}

int stop_processes(struct stop_process* processes, int count, int grace_ms, int kill_ms) {
    int signal_error = 0;
    int remaining;

    for (int i = 0; i < count; ++i) {
        struct stop_process* process = &processes[i];

        int error;

        process->status    = 0;
        process->exited    = process->pid < 0;
        process->escalated = 0;

        if (process->exited)
            continue;

        error = deliver(process, SIGTERM);

        if (error && !signal_error)
            signal_error = error;
    }

    remaining = wait_for_exit(processes, count, grace_ms);
    if (remaining) {
        for (int i = 0; i < count; ++i) {
            struct stop_process* process = &processes[i];

            int error;

            if (process->exited)
                continue;

            process->escalated = 1;

            error = deliver(process, SIGKILL);

            if (error && !signal_error)
                signal_error = error;
        }

        remaining = wait_for_exit(processes, count, kill_ms);
    }

    if (remaining) {
        errno = signal_error ? signal_error : ETIMEDOUT;

        return -1;
    }

    return 0;
}
