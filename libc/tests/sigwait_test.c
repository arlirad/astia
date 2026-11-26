#include <errno.h>
#include <signal.h>
#include <stdio.h>

static int          calls;
static unsigned int observed_set;

long __astia_process_sigwait(const unsigned int* set, int* signal) {
    ++calls;

    observed_set = *set;
    *signal      = SIGUSR1;

    return 0;
}

long __astia_process_sigsuspend(const unsigned int* mask) {
    (void) mask;

    return 0;
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
    sigset_t set;

    int signal = 0;

    CHECK(sigemptyset(&set) == 0);
    CHECK(sigaddset(&set, SIGUSR1) == 0);
    CHECK(sigwait(&set, &signal) == 0);
    CHECK(calls == 1);
    CHECK(observed_set == set);
    CHECK(signal == SIGUSR1);

    return fails != 0;
}
