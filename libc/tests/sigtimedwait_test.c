#include "../src/process_port.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>

static int                    calls;
static const sigset_t*        observed_set;
static siginfo_t*             observed_info;
static const struct timespec* observed_timeout;
static long                   result;

long __astia_process_sigtimedwait(const unsigned int* set, astia_siginfo_t* info,
                                  const void* timeout) {
    ++calls;

    observed_set     = (const sigset_t*) set;
    observed_info    = (siginfo_t*) info;
    observed_timeout = (const struct timespec*) timeout;

    if (info != NULL) {
        info->signo       = SIGUSR1;
        info->errno_value = 7;
        info->code        = 8;
        info->pid         = 9;
        info->uid         = 10;
        info->status      = 11;
    }

    return result;
}

static int fails;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++fails;                                                    \
        }                                                               \
    } while (0)

int main(void) {
    sigset_t        set;
    siginfo_t       info    = {0};
    struct timespec timeout = {2, 3};

    CHECK(sigemptyset(&set) == 0);
    CHECK(sigaddset(&set, SIGUSR1) == 0);

    result = SIGUSR1;

    CHECK(sigtimedwait(&set, &info, &timeout) == SIGUSR1);
    CHECK(calls == 1);
    CHECK(observed_set == &set);
    CHECK(observed_info != NULL);
    CHECK(observed_timeout != NULL);
    CHECK(observed_timeout->tv_sec == timeout.tv_sec);
    CHECK(observed_timeout->tv_nsec == timeout.tv_nsec);
    CHECK(info.si_signo == SIGUSR1 && info.si_errno == 7 && info.si_code == 8 &&
          info.si_pid == 9 && info.si_uid == 10 && info.si_status == 11);

    result = SIGUSR1;

    CHECK(sigwaitinfo(&set, &info) == SIGUSR1);
    CHECK(calls == 2);
    CHECK(observed_set == &set);
    CHECK(observed_info != NULL);
    CHECK(observed_timeout == NULL);
    CHECK(info.si_signo == SIGUSR1 && info.si_errno == 7 && info.si_code == 8 &&
          info.si_pid == 9 && info.si_uid == 10 && info.si_status == 11);

    result = -EAGAIN;
    errno  = 0;

    CHECK(sigwaitinfo(&set, NULL) == -1);
    CHECK(errno == EAGAIN);
    CHECK(calls == 3);
    CHECK(observed_info == NULL);
    CHECK(observed_timeout == NULL);

    errno = 0;

    CHECK(sigtimedwait(&set, NULL, NULL) == -1);
    CHECK(errno == EAGAIN);
    CHECK(calls == 4);
    CHECK(observed_info == NULL);
    CHECK(observed_timeout == NULL);

    return fails != 0;
}
