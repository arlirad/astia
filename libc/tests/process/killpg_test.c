#include "internal/process_port.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>

static int  failures;
static int  observed_pid;
static int  observed_signal;
static long kill_result;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

long __astia_process_kill(int pid, int signal) {
    observed_pid    = pid;
    observed_signal = signal;

    return kill_result;
}

long __astia_process_sigwait(const unsigned int* set, int* signal) {
    (void) set;
    (void) signal;

    return -ENOSYS;
}

long __astia_process_sigsuspend(const unsigned int* mask) {
    (void) mask;

    return -ENOSYS;
}

int main(void) {
    kill_result = 0;
    errno       = EINTR;

    CHECK(killpg(37, SIGUSR1) == 0);
    CHECK(observed_pid == -37);
    CHECK(observed_signal == SIGUSR1);

    kill_result = -ESRCH;
    errno       = 0;

    CHECK(killpg(999999, 0) == -1 && errno == ESRCH);

    kill_result = -EPERM;
    errno       = 0;

    CHECK(killpg(37, SIGTERM) == -1 && errno == EPERM);

    errno = 0;

    CHECK(killpg(-1, SIGTERM) == -1 && errno == EINVAL);
    CHECK(observed_pid == -37 && observed_signal == SIGTERM);

    return failures != 0;
}
