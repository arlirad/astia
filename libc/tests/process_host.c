#include "../src/process_port.h"
#include <sys/syscall.h>

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

long __astia_process_gettid(void) {
    return host_trap(SYS_gettid, 0, 0, 0, 0);
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
    int   child           = 0;
    int   rc = posix_spawn(&child, path, NULL, NULL, argv == NULL ? fallback_argv : argv,
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
