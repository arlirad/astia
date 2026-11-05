#include "../src/process_port.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>

static int                fails;
static int                observed_signal;
static struct sigaction   observed_action;
static int                observed_how;
static sigset_t            observed_set;
static sigset_t            suspend_mask;

#define CHECK(condition)                                                   \
    do {                                                                    \
        if (!(condition)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++fails;                                                        \
        }                                                                   \
    } while (0)

long __astia_process_sigaction(int signal, const astia_sigaction_t* action,
                               astia_sigaction_t* old_action) {
    observed_signal = signal;
    if (action != NULL)
        observed_action = *(const struct sigaction*) action;
    if (old_action != NULL)
        *(struct sigaction*) old_action = (struct sigaction) {SIG_DFL, 0, 0, 0};
    return 0;
}

long __astia_process_sigprocmask(int how, unsigned int set, unsigned int* old_set) {
    observed_how = how;
    observed_set = set;
    if (old_set != NULL)
        *old_set = (1u << SIGUSR1) | (1u << SIGTERM);
    return 0;
}

long __astia_process_sigsuspend(const unsigned int* mask) {
    suspend_mask = *mask;
    return -EINTR;
}

int main(void) {
    sigset_t old;

    CHECK(sigignore(SIGUSR1) == 0);
    CHECK(observed_signal == SIGUSR1);
    CHECK(observed_action.sa_handler == SIG_IGN);

    CHECK(sigprocmask(SIG_SETMASK, NULL, &old) == 0);
    CHECK(observed_how == SIG_BLOCK);
    CHECK(observed_set == 0);
    CHECK(old == ((1u << SIGUSR1) | (1u << SIGTERM)));

    errno = 0;
    CHECK(sigpause(SIGUSR1) == -1 && errno == EINTR);
    CHECK((suspend_mask & (1u << SIGUSR1)) == 0);
    CHECK((suspend_mask & (1u << SIGTERM)) != 0);
    return fails != 0;
}
