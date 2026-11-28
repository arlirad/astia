#include "internal/process_port.h"
#include <sys/syscall.h>

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>

extern long syscall(long number, ...);

static int           fails;
static volatile int  worker_ready;
static volatile int  worker_release;
static volatile long worker_tid;
static long          observed_tid;
static int           observed_signal;
static int           kill_calls;

#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++fails;                                                    \
        }                                                               \
    } while (0)

static long host_tid(void) {
    return syscall(SYS_gettid);
}

long __astia_process_gettid(void) {
    return host_tid();
}

long __astia_process_thread_kill(unsigned long tid, int signal) {
    observed_tid    = (long) tid;
    observed_signal = signal;

    return 0;
}

long __astia_process_kill(int pid, int signal) {
    (void) pid;
    (void) signal;

    ++kill_calls;

    return 0;
}

long __astia_process_sigaction(int signal, const struct astia_sigaction* action,
                               struct astia_sigaction* old_action) {
    (void) signal;
    (void) action;
    (void) old_action;

    return 0;
}

long __astia_process_sigprocmask(int how, unsigned int set, unsigned int* old_set) {
    (void) how;
    (void) set;
    (void) old_set;

    return 0;
}

long __astia_process_sigsuspend(const unsigned int* mask) {
    (void) mask;

    return -EINTR;
}

long __astia_process_sigwait(const unsigned int* set, int* signal) {
    (void) set;
    (void) signal;

    return -ENOSYS;
}

long __astia_process_sigtimedwait(const unsigned int* set, astia_siginfo_t* info,
                                  const void* timeout) {
    (void) set;
    (void) info;
    (void) timeout;

    return -ENOSYS;
}

long __astia_process_sigpending(unsigned int* set) {
    if (set != NULL)
        *set = 0;

    return 0;
}

static void* worker(void* opaque) {
    (void) opaque;

    worker_tid   = host_tid();
    worker_ready = 1;

    while (!worker_release) {
        sched_yield();
    }

    return NULL;
}

int main(void) {
    pthread_t thread = 0;

    long caller;

    CHECK(pthread_create(&thread, NULL, worker, NULL) == 0);

    while (!worker_ready) {
        sched_yield();
    }

    caller = host_tid();

    CHECK(caller != worker_tid);

    CHECK(raise(SIGUSR1) == 0);
    CHECK(observed_tid == caller);
    CHECK(observed_signal == SIGUSR1);
    CHECK(kill_calls == 0);

    observed_tid    = 0;
    observed_signal = -1;

    CHECK(raise(0) == 0);
    CHECK(observed_tid == caller);
    CHECK(observed_signal == 0);

    errno = 0;

    CHECK(raise(-1) == -1 && errno == EINVAL);

    errno = 0;

    CHECK(raise(NSIG) == -1 && errno == EINVAL);
    CHECK(observed_tid == caller);

    worker_release = 1;

    CHECK(pthread_join(thread, NULL) == 0);

    return fails != 0;
}
