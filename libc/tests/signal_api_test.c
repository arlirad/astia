#include "../src/process_port.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>

static int              fails;
static int              observed_signal;
static struct sigaction observed_action;
static int              observed_how;
static sigset_t         observed_set;
static sigset_t         suspend_mask;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++fails;                                                    \
        }                                                               \
    } while (0)

long __astia_process_sigaction(int signal, const astia_sigaction_t* action,
                               astia_sigaction_t* old_action) {
    observed_signal = signal;

    if (action != NULL)
        observed_action = *(const struct sigaction*) action;

    if (old_action != NULL)
        *(struct sigaction*) old_action = (struct sigaction){SIG_DFL, 0, 0, 0};

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
    sigset_t         old;
    struct sigaction restart_action = {0};

    CHECK(sigignore(SIGUSR1) == 0);
    CHECK(observed_signal == SIGUSR1);
    CHECK(observed_action.sa_handler == SIG_IGN);

    restart_action.sa_handler = SIG_DFL;
    restart_action.sa_flags   = SA_RESTART;

    CHECK(sigemptyset(&restart_action.sa_mask) == 0);
    CHECK(sigaction(SIGUSR1, &restart_action, NULL) == 0);
    CHECK(observed_action.sa_flags == SA_RESTART);

    restart_action.sa_flags = SA_NODEFER | SA_RESETHAND;

    CHECK(sigaction(SIGUSR1, &restart_action, NULL) == 0);
    CHECK(observed_action.sa_flags == (SA_NODEFER | SA_RESETHAND));

    CHECK(sigprocmask(SIG_SETMASK, NULL, &old) == 0);
    CHECK(observed_how == SIG_BLOCK);
    CHECK(observed_set == 0);
    CHECK(old == ((1u << SIGUSR1) | (1u << SIGTERM)));

    CHECK(sighold(SIGUSR1) == 0);
    CHECK(observed_how == SIG_BLOCK);
    CHECK(observed_set == (1u << SIGUSR1));
    CHECK(sigrelse(SIGUSR1) == 0);
    CHECK(observed_how == SIG_UNBLOCK);
    CHECK(observed_set == (1u << SIGUSR1));

    errno = 0;

    CHECK(sighold(0) == -1 && errno == EINVAL);

    errno = 0;

    CHECK(sigrelse(NSIG) == -1 && errno == EINVAL);

    CHECK(SIGAIO == 16);
    CHECK(SIGIO == 29);
    CHECK(SIGPOLL == SIGIO);
    CHECK(NSIG == 32);

    CHECK(sigaction(SIGAIO, &restart_action, NULL) == 0);
    CHECK(observed_signal == SIGAIO);
    CHECK(sighold(SIGAIO) == 0);
    CHECK(observed_how == SIG_BLOCK);
    CHECK(observed_set == (1u << SIGAIO));
    CHECK(sigrelse(SIGAIO) == 0);
    CHECK(observed_how == SIG_UNBLOCK);
    CHECK(observed_set == (1u << SIGAIO));

    errno = 0;

    CHECK(sigpause(SIGUSR1) == -1 && errno == EINTR);
    CHECK((suspend_mask & (1u << SIGUSR1)) == 0);
    CHECK((suspend_mask & (1u << SIGTERM)) != 0);

    return fails != 0;
}
