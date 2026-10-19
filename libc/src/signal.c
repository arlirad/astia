#include "process_port.h"
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <astia/api/signal.h>
#include <astia/api/syscalls.h>

int sigemptyset(sigset_t* set) {
    if (set == NULL) {
        errno = EINVAL;
        return -1;
    }
    *set = 0;
    return 0;
}

int sigfillset(sigset_t* set) {
    if (set == NULL) {
        errno = EINVAL;
        return -1;
    }
    *set = 0xFFFFFFFEu;
    return 0;
}

int sigaddset(sigset_t* set, int signal_number) {
    if (set == NULL || signal_number <= 0 || signal_number >= 32) {
        errno = EINVAL;
        return -1;
    }
    *set |= 1u << signal_number;
    return 0;
}

int sigdelset(sigset_t* set, int signal_number) {
    if (set == NULL || signal_number <= 0 || signal_number >= 32) {
        errno = EINVAL;
        return -1;
    }
    *set &= ~(1u << signal_number);
    return 0;
}

int sigismember(const sigset_t* set, int signal_number) {
    if (set == NULL || signal_number <= 0 || signal_number >= 32) {
        errno = EINVAL;
        return -1;
    }
    return (*set & (1u << signal_number)) != 0;
}

int sigaction(int signal_number, const struct sigaction* action,
              struct sigaction* old_action) {
    const astia_sigaction_t* next = (const astia_sigaction_t*) action;
    astia_sigaction_t*       old  = (astia_sigaction_t*) old_action;
    const long rc = astia_sigaction(signal_number, next, old);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}

int sigprocmask(int how, const sigset_t* set, sigset_t* old_set) {
    const uint32_t value = set == NULL ? 0 : *set;
    const long rc = astia_sigprocmask(how, value, (uint32_t*) old_set);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}

int kill(int pid, int signal_number) {
    /* Job-control probes and SIGCONT are advisory until process groups are
       fully implemented; do not turn a completed foreground job into a
       shell error. SIGCHLD/reaping uses the kernel wait path separately. */
    if (pid <= 0 || signal_number == SIGCONT || signal_number == SIGTSTP)
        return 0;
    const long rc = astia_kill(pid, signal_number);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}

int raise(int signal_number) {
    return kill((int) astia_getpid(), signal_number);
}

int sigsuspend(const sigset_t* mask) {
    const long rc = __astia_process_sigsuspend(mask);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    /* POSIX sigsuspend only returns after interruption. */
    errno = EINTR;
    return -1;
}

sighandler_t signal(int signal_number, sighandler_t handler) {
    struct sigaction next = {handler, 0, 0};
    struct sigaction old  = {0, 0, 0};
    if (sigaction(signal_number, &next, &old) != 0) {
        return SIG_ERR;
    }
    return old.sa_handler;
}
