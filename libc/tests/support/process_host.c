#include "internal/process_port.h"
#include <sys/syscall.h>
#include <sys/utsname.h>

#include <errno.h>
#include <stddef.h>

/* Raw host syscalls, not libc calls: this object lives in test
 * binaries that also contain this libc's getpid/waitpid/_Exit, so a
 * call by libc name could bind to our own definitions (or recurse
 * through the port).  Note glibc's syscall() wrapper itself normalizes
 * errors to -1 + errno, while the seam's contract is the ABI's
 * negated error code — host_trap() below converts.  Host errno is read
 * through __errno_location (like backend_host.c): <errno.h> on this
 * include path is *ours* and glibc never touches it. */
extern long syscall(long number, ...);
extern int* __errno_location(void); /* glibc's errno, without its header */
#define HOST_ERRNO (*__errno_location())

static long host_trap(long number, long a1, long a2, long a3, long a4) {
    long rc = syscall(number, a1, a2, a3, a4);

    if (rc == -1) /* none of these calls may legitimately return -1 */
        return -HOST_ERRNO;

    return rc;
}

void __astia_process_exit(int status) {
    (void) host_trap(SYS_exit, status, 0, 0, 0);
}

long __astia_process_getpid(void) {
    return host_trap(SYS_getpid, 0, 0, 0, 0);
}

long __astia_process_getppid(void) {
    return host_trap(SYS_getppid, 0, 0, 0, 0);
}

long __astia_process_gettid(void) {
    return host_trap(SYS_gettid, 0, 0, 0, 0);
}

long __astia_process_fchdir(int fd) {
    return host_trap(SYS_fchdir, fd, 0, 0, 0);
}

struct host_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

_Static_assert(sizeof(struct host_utsname) == 390, "host utsname layout");

long __astia_process_uname(struct utsname* name) {
    struct host_utsname host;

    long rc;

    if (name == NULL)
        return -EFAULT;

    rc = syscall(SYS_uname, &host);

    if (rc == -1)
        return -HOST_ERRNO;

    __builtin_memset(name, 0, sizeof *name);

    for (size_t i = 0; i < sizeof name->sysname; ++i) {
        name->sysname[i] = host.sysname[i];
    }

    for (size_t i = 0; i < sizeof name->nodename; ++i) {
        name->nodename[i] = host.nodename[i];
    }

    for (size_t i = 0; i < sizeof name->release; ++i) {
        name->release[i] = host.release[i];
    }

    for (size_t i = 0; i < sizeof name->version; ++i) {
        name->version[i] = host.version[i];
    }

    for (size_t i = 0; i < sizeof name->machine; ++i) {
        name->machine[i] = host.machine[i];
    }

    return 0;
}

long __astia_process_getuid(void) {
    return host_trap(SYS_getuid, 0, 0, 0, 0);
}

long __astia_process_geteuid(void) {
    return host_trap(SYS_geteuid, 0, 0, 0, 0);
}

long __astia_process_getgid(void) {
    return host_trap(SYS_getgid, 0, 0, 0, 0);
}

long __astia_process_getegid(void) {
    return host_trap(SYS_getegid, 0, 0, 0, 0);
}

__attribute__((weak)) long __astia_process_getgroups(int count, unsigned int* groups) {
    return host_trap(SYS_getgroups, count, (long) groups, 0, 0);
}

__attribute__((weak)) long __astia_process_setgroups(int                 count,
                                                     const unsigned int* groups) {
    return host_trap(SYS_setgroups, count, (long) groups, 0, 0);
}

long __astia_process_getpgrp(void) {
    return host_trap(SYS_getpgrp, 0, 0, 0, 0);
}

long __astia_process_getpgid(int pid) {
    return host_trap(SYS_getpgid, pid, 0, 0, 0);
}

long __astia_process_setpgid(int pid, int process_group) {
    return host_trap(SYS_setpgid, pid, process_group, 0, 0);
}

__attribute__((weak)) long __astia_process_kill(int pid, int signal) {
    return host_trap(SYS_kill, pid, signal, 0, 0);
}

long __astia_process_yield(void) {
    return host_trap(SYS_sched_yield, 0, 0, 0, 0);
}

long __astia_process_waitpid(int pid, int* status, int options) {
    return host_trap(SYS_wait4, pid, (long) status, options, 0);
}

/* The one seam function that goes through a host *library* call: the
 * raw clone+execve sequence is exactly what posix_spawn encapsulates.
 * Declared by hand because glibc's <spawn.h> spells posix_spawnattr_t
 * with `struct sched_param` — its <sched.h> — and on this include path
 * <sched.h> is ours and has no such type.  The ABI is all pointers, and
 * our libc exports no posix_spawn, so the name binds to glibc's: no
 * recursion possible.  Null vectors are legal for the astia side
 * (empty); posix_spawn demands real ones.  errno-style in, ABI's
 * negated code out. */
extern int posix_spawn(int* pid, const char* path, const void* file_actions,
                       const void* attr, char* const argv[], char* const envp[]);

long __astia_process_spawnve(const char* path, char* const argv[], char* const envp[]) {
    char* fallback_argv[] = {(char*) path, NULL};
    char* empty_env[]     = {NULL};

    int child = 0;
    int rc    = posix_spawn(&child, path, NULL, NULL, argv == NULL ? fallback_argv : argv,
                         envp == NULL ? empty_env : envp);

    return rc == 0 ? (long) child : -(long) rc;
}

long __astia_process_execve(const char* path, char* const argv[], char* const envp[]) {
    return host_trap(SYS_execve, (long) path, (long) argv, (long) envp, 0);
}

long __astia_process_fork(void) {
    /* Raw host fork, not our fork(): by libc name it would bind to the
     * process.c wrapper above and recurse through this seam. */
    return host_trap(SYS_fork, 0, 0, 0, 0);
}

long __astia_process_sigaction(int signal, const astia_sigaction_t* action,
                               astia_sigaction_t* old_action) {
    (void) signal;
    (void) action;
    (void) old_action;

    return -ENOSYS;
}

long __astia_process_sigprocmask(int how, unsigned int set, unsigned int* old_set) {
    (void) how;
    (void) set;
    (void) old_set;

    return -ENOSYS;
}

long __astia_process_sigpending(unsigned int* set) {
    unsigned long host_set = 0;
    long          rc       = syscall(SYS_rt_sigpending, &host_set, sizeof host_set);

    if (rc == -1)
        return -HOST_ERRNO;

    if (set != NULL)
        *set = (unsigned int) host_set;

    return rc;
}

long __astia_process_sigtimedwait(const unsigned int* set, astia_siginfo_t* info,
                                  const void* timeout) {
    unsigned long host_set = set == NULL ? 0 : (unsigned long) *set;

    struct {
        int          signo;
        int          errno_value;
        int          code;
        int          pid;
        unsigned int uid;
        int          status;
    } host_info;

    long rc = syscall(SYS_rt_sigtimedwait, &host_set, info == NULL ? NULL : &host_info,
                      timeout, sizeof host_set);

    if (rc == -1)
        return -HOST_ERRNO;

    if (info != NULL) {
        info->signo       = host_info.signo;
        info->errno_value = host_info.errno_value;
        info->code        = host_info.code;
        info->pid         = host_info.pid;
        info->uid         = host_info.uid;
        info->status      = host_info.status;
    }

    return rc;
}
