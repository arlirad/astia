#include "process_port.h"
#include <astia/api/signal.h>
#include <astia/api/syscalls.h>

#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

_Thread_local unsigned int __astia_signal_handler_depth;
static struct sigaction   astia_signal_actions[NSIG];

static void astia_signal_dispatch(int signal_number, siginfo_t* info, void* context) {
    const struct sigaction action = astia_signal_actions[signal_number];
    ++__astia_signal_handler_depth;
    if ((action.sa_flags & SA_SIGINFO) != 0) {
        if (action.sa_sigaction != NULL)
            action.sa_sigaction(signal_number, info, context);
    }
    else if (action.sa_handler != NULL && action.sa_handler != SIG_DFL &&
             action.sa_handler != SIG_IGN) {
        action.sa_handler(signal_number);
    }
    --__astia_signal_handler_depth;
}

__attribute__((noreturn, naked, visibility("hidden"))) void
astia_signal_trampoline(void) {
    __asm__ volatile("leaq (%rsp), %rdi\n"
                     "movl $29, %eax\n"
                     "syscall\n"
                     "ud2\n");
}

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
    if (set == NULL || signal_number <= 0 || signal_number >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    *set |= 1u << signal_number;
    return 0;
}

int sigdelset(sigset_t* set, int signal_number) {
    if (set == NULL || signal_number <= 0 || signal_number >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    *set &= ~(1u << signal_number);
    return 0;
}

int sigismember(const sigset_t* set, int signal_number) {
    if (set == NULL || signal_number <= 0 || signal_number >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    return (*set & (1u << signal_number)) != 0;
}

int sigaction(int signal_number, const struct sigaction* action,
              struct sigaction* old_action) {
    astia_sigaction_t        next_value;
    const astia_sigaction_t* next = NULL;
    struct sigaction         previous = {0};
    const int                 valid = signal_number > 0 && signal_number < NSIG;
    if (!valid) {
        errno = EINVAL;
        return -1;
    }
    if (action != NULL) {
        next_value.handler = (uintptr_t) ((action->sa_flags & SA_SIGINFO) != 0
                                              ? (void*) action->sa_sigaction
                                              : (void*) action->sa_handler);
        if (next_value.handler != (uintptr_t) SIG_DFL &&
            next_value.handler != (uintptr_t) SIG_IGN)
            next_value.handler = (uintptr_t) &astia_signal_dispatch;
        next_value.mask    = action->sa_mask;
        next_value.flags   = (uint32_t) action->sa_flags;
        /* sa_restorer is not part of the POSIX interface.  In particular,
           callers such as zsh initialize the handler, mask, and flags but
           leave this extension member indeterminate.  Do not pass that
           stack garbage to the kernel: the libc trampoline is the ABI
           restorer for every ordinary sigaction call. */
        next_value.restorer = (uintptr_t) &astia_signal_trampoline;
        next                = &next_value;
    }
    if (old_action != NULL)
        previous = astia_signal_actions[signal_number];
    const long rc = __astia_process_sigaction(signal_number, next, NULL);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    if (old_action != NULL)
        *old_action = previous;
    if (action != NULL)
        astia_signal_actions[signal_number] = *action;
    return 0;
}

int sigaltstack(const stack_t* stack, stack_t* old_stack) {
    astia_stack_t next;
    astia_stack_t old;
    const astia_stack_t* input = NULL;
    if (stack != NULL) {
        next.sp       = (uintptr_t) stack->ss_sp;
        next.size     = stack->ss_size;
        next.flags    = stack->ss_flags;
        next.reserved = 0;
        input         = &next;
    }
    const long rc = astia_sigaltstack(input, old_stack == NULL ? NULL : &old);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    if (old_stack != NULL) {
        old_stack->ss_sp    = (void*) old.sp;
        old_stack->ss_size  = old.size;
        old_stack->ss_flags = old.flags;
    }
    return 0;
}

int sigprocmask(int how, const sigset_t* set, sigset_t* old_set) {
    const uint32_t value = set == NULL ? 0 : *set;
    const int      operation = set == NULL ? SIG_BLOCK : how;
    const long rc = __astia_process_sigprocmask(operation, value, (uint32_t*) old_set);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}

int sigpending(sigset_t* set) {
    const long rc = __astia_process_sigpending((unsigned int*) set);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}

static int astia_valid_signal_number(int signal_number) {
    return signal_number > 0 && signal_number < NSIG && signal_number != 16;
}

int sighold(int signal_number) {
    sigset_t set;
    if (!astia_valid_signal_number(signal_number)) {
        errno = EINVAL;
        return -1;
    }
    if (sigemptyset(&set) < 0 || sigaddset(&set, signal_number) < 0)
        return -1;
    return sigprocmask(SIG_BLOCK, &set, NULL);
}

int sigrelse(int signal_number) {
    sigset_t set;
    if (!astia_valid_signal_number(signal_number)) {
        errno = EINVAL;
        return -1;
    }
    if (sigemptyset(&set) < 0 || sigaddset(&set, signal_number) < 0)
        return -1;
    return sigprocmask(SIG_UNBLOCK, &set, NULL);
}

int sigwait(const sigset_t* set, int* signal) {
    const long rc = __astia_process_sigwait(set, signal);
    return rc < 0 ? (int) -rc : 0;
}

static void astia_copy_siginfo(siginfo_t* destination, const astia_siginfo_t* source) {
    destination->si_signo  = source->signo;
    destination->si_errno  = source->errno_value;
    destination->si_code   = source->code;
    destination->si_pid    = source->pid;
    destination->si_uid    = source->uid;
    destination->si_status = source->status;
    destination->si_addr   = (void*) source->addr;
}

int sigwaitinfo(const sigset_t* set, siginfo_t* info) {
    astia_siginfo_t observed = {0};
    const long rc = __astia_process_sigtimedwait(set, info == NULL ? NULL : &observed, NULL);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    if (info != NULL)
        astia_copy_siginfo(info, &observed);
    return (int) rc;
}

int sigtimedwait(const sigset_t* set, siginfo_t* info, const struct timespec* timeout) {
    astia_siginfo_t observed = {0};
    astia_timespec  requested;
    const astia_timespec* requested_ptr = NULL;
    if (timeout != NULL) {
        requested.tv_sec  = timeout->tv_sec;
        requested.tv_nsec = timeout->tv_nsec;
        requested_ptr     = &requested;
    }
    const long rc = __astia_process_sigtimedwait(
        set, info == NULL ? NULL : &observed, requested_ptr);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    if (info != NULL) {
        astia_copy_siginfo(info, &observed);
    }
    return (int) rc;
}

int sigignore(int signal_number) {
    struct sigaction action = {SIG_IGN, 0, 0, 0};
    return sigaction(signal_number, &action, NULL);
}

int sigpause(int signal_number) {
    sigset_t mask;
    if (sigprocmask(SIG_BLOCK, NULL, &mask) < 0)
        return -1;
    if (sigdelset(&mask, signal_number) < 0)
        return -1;
    return sigsuspend(&mask);
}

int kill(int pid, int signal_number) {
    const long rc = __astia_process_kill(pid, signal_number);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}

int killpg(int process_group, int signal_number) {
    if (process_group < 0) {
        errno = EINVAL;
        return -1;
    }
    return kill(-process_group, signal_number);
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
    struct sigaction next = {handler, 0, 0, 0};
    struct sigaction old  = {0, 0, 0, 0};
    if (sigaction(signal_number, &next, &old) != 0) {
        return SIG_ERR;
    }
    return old.sa_handler;
}
