#include "process_port.h"
#include <astia/api/signal.h>
#include <astia/api/syscalls.h>

#include <errno.h>
#include <signal.h>
#include <unistd.h>

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
    if (action != NULL) {
        next_value.handler = (uintptr_t) ((action->sa_flags & SA_SIGINFO) != 0
                                              ? (void*) action->sa_sigaction
                                              : (void*) action->sa_handler);
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
    astia_sigaction_t* old = (astia_sigaction_t*) old_action;
    const long         rc  = __astia_process_sigaction(signal_number, next, old);
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
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

int sigwait(const sigset_t* set, int* signal) {
    const long rc = __astia_process_sigwait(set, signal);
    return rc < 0 ? (int) -rc : 0;
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
    const long rc = astia_kill(pid, signal_number);
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
