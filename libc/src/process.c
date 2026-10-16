#include "process_port.h"
#include <sys/wait.h>

#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

enum { ATEXIT_MAX = 32 };
static void (*g_atexit[ATEXIT_MAX])(void);
static size_t g_atexit_count;

/* Every wrapper speaks the seam's convention: >= 0, or negated error
 * code — mapped here to the -1/errno form the standard demands. */
static int process_result(long rc) {
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return (int) rc;
}

int getpid(void) {
    return process_result(__astia_process_getpid());
}

int gettid(void) {
    return process_result(__astia_process_gettid());
}

int sched_yield(void) {
    long rc = __astia_process_yield();
    if (rc < 0) {
        errno = (int) -rc;
        return -1;
    }
    return 0;
}

int waitpid(int pid, int* status, int options) {
    if (options != 0) { /* WNOHANG and friends are not in the ABI yet */
        errno = EINVAL;
        return -1;
    }
    return process_result(__astia_process_waitpid(pid, status));
}

int spawnve(const char* path, char* const argv[], char* const envp[]) {
    return process_result(__astia_process_spawnve(path, argv, envp));
}

int fork(void) {
    /* The kernel resumes the child at this return with rax = 0 — the
     * seam's success value — so the standard child/parent split falls
     * out of the raw result. */
    return process_result(__astia_process_fork());
}

int atexit(void (*function)(void)) {
    if (function == NULL || g_atexit_count == ATEXIT_MAX)
        return -1;
    g_atexit[g_atexit_count++] = function;
    return 0;
}

_Noreturn void _Exit(int status) {
    __astia_process_exit(status);
    __builtin_unreachable();
}

_Noreturn void exit(int status) {
    while (g_atexit_count != 0)
        g_atexit[--g_atexit_count]();
    fflush(NULL); /* ISO C: streams flush after the atexit handlers */
    _Exit(status);
}
