#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int          sig_atomic_t;
typedef unsigned int sigset_t;
typedef void (*sighandler_t)(int);
typedef struct siginfo {
    int          si_signo;
    int          si_errno;
    int          si_code;
    int          si_pid;
    unsigned int si_uid;
    int          si_status;
} siginfo_t;
typedef void (*sigaction_handler_t)(int, siginfo_t*, void*);
typedef struct {
    void*         ss_sp;
    unsigned long ss_size;
    int           ss_flags;
} stack_t;

struct sigaction {
    union {
        sighandler_t        sa_handler;
        sigaction_handler_t sa_sigaction;
    };
    sigset_t     sa_mask;
    int          sa_flags;
    uintptr_t    sa_restorer;
};

#define SIG_DFL ((sighandler_t) 0)
#define SIG_IGN ((sighandler_t) 1)
#define SIG_ERR ((sighandler_t) - 1)
#define SA_ONSTACK 1
#define SA_SIGINFO 4
#define SS_ONSTACK 1
#define SS_DISABLE 2
#define MINSIGSTKSZ 2048
#define SIGSTKSZ 8192

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGIO 29
#define SIGAIO SIGIO
#define SIGPWR 30
#define SIGSYS 31
#define NSIG 32

#define CLD_EXITED 1
#define CLD_KILLED 2
#define CLD_DUMPED 3
#define CLD_STOPPED 5
#define CLD_CONTINUED 6

int sigaction(int signal, const struct sigaction* action, struct sigaction* old_action);
int sigaltstack(const stack_t* stack, stack_t* old_stack);
int sigprocmask(int how, const sigset_t* set, sigset_t* old_set);
int sigpending(sigset_t* set);
int sigwait(const sigset_t* set, int* signal);
int sigignore(int signal);
int sigpause(int signal);
int kill(int pid, int signal);
int killpg(int process_group, int signal);
int raise(int signal);
int sigsuspend(const sigset_t* mask);
sighandler_t signal(int signal, sighandler_t handler);

int sigemptyset(sigset_t* set);
int sigfillset(sigset_t* set);
int sigaddset(sigset_t* set, int signal);
int sigdelset(sigset_t* set, int signal);
int sigismember(const sigset_t* set, int signal);

#ifdef __cplusplus
}
#endif
