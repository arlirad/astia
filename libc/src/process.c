#include "file_port.h"
#include "process_port.h"
#include <astia/api/syscalls.h>
#include <sys/wait.h>

#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

enum { ATEXIT_MAX = 32 };
static void (*g_atexit[ATEXIT_MAX])(void);
static size_t g_atexit_count;

/* Freestanding C++ runtimes may emit these even when no hosted runtime is
 * linked.  Keep the ABI available to mixed C/C++ user images; C atexit
 * handlers retain the normal libc behavior, while destructor callbacks are
 * run at exit with their registration argument. */
void* __dso_handle __attribute__((visibility("hidden"), weak));
struct cxa_atexit_entry {
    void (*function)(void*);
    void* argument;
};
static struct cxa_atexit_entry g_cxa_atexit[ATEXIT_MAX];
static size_t                  g_cxa_atexit_count;

int __cxa_atexit(void (*function)(void*), void* argument, void* dso_handle) {
    (void) dso_handle;
    if (function == NULL || g_cxa_atexit_count == ATEXIT_MAX)
        return -1;
    g_cxa_atexit[g_cxa_atexit_count++] = (struct cxa_atexit_entry) {function, argument};
    return 0;
}
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

int getppid(void) {
    errno = ENOSYS;
    return -1;
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
    if (options & ~(WNOHANG | WUNTRACED | WCONTINUED)) {
        errno = EINVAL;
        return -1;
    }
    int  local  = 0;
    int* output = status == NULL ? &local : status;
    int  result = process_result(__astia_process_waitpid(pid, output, options));
    return result;
}

int wait(int* status) {
    return waitpid(-1, status, 0);
}

int spawnve(const char* path, char* const argv[], char* const envp[]) {
    /* A null environment is the convenient inheritance form used by the
     * shell.  The kernel ABI accepts an explicit empty vector as well, so
     * resolve this libc-level shorthand here. */
    return process_result(
        __astia_process_spawnve(path, argv, envp == NULL ? environ : envp));
}

int fork(void) {
    /* The kernel resumes the child at this return with rax = 0 — the
     * seam's success value — so the standard child/parent split falls
     * out of the raw result. */
    int result = process_result(__astia_process_fork());
    if (result == 0) {
        __astia_fio_after_fork();
    }
    else if (result > 0) {
        /* The child inherited the ring alias as part of its address space,
           but cannot use the parent's process-owned slot.  Reset its lazy
           handle without destroying the parent's slot; the parent can drop
           its own ring only after the fork has cloned the address space. */
        __astia_fio_prepare_fork();
    }
    return result;
}

int setuid(unsigned int uid) {
    return process_result(astia_setuid(uid));
}
unsigned int getuid(void) {
    return (unsigned int) process_result(astia_getuid());
}
unsigned int geteuid(void) {
    return (unsigned int) process_result(astia_geteuid());
}
int seteuid(unsigned int uid) {
    return process_result(astia_seteuid(uid));
}
int setgid(unsigned int gid) {
    return process_result(astia_setgid(gid));
}
unsigned int getgid(void) {
    return (unsigned int) process_result(astia_getgid());
}
unsigned int getegid(void) {
    return (unsigned int) process_result(astia_getegid());
}
int setegid(unsigned int gid) {
    return process_result(astia_setegid(gid));
}
int setreuid(unsigned int real_uid, unsigned int effective_uid) {
    return process_result(astia_setreuid(real_uid, effective_uid));
}
int setregid(unsigned int real_gid, unsigned int effective_gid) {
    return process_result(astia_setregid(real_gid, effective_gid));
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
    while (g_cxa_atexit_count != 0) {
        struct cxa_atexit_entry entry = g_cxa_atexit[--g_cxa_atexit_count];
        entry.function(entry.argument);
    }
    while (g_atexit_count != 0)
        g_atexit[--g_atexit_count]();
    fflush(NULL); /* ISO C: streams flush after the atexit handlers */
    _Exit(status);
}
