#pragma once

/*
 * astia/api/syscalls.h — raw syscall layer of the userspace ABI.
 *
 * Contents: the architecture-independent SYS_* number table, the
 * astia_raw6() trap, typed inline wrappers for the direct calls, the flag
 * constants those calls consume, and astia_timespec.
 *
 * Direct calls return at once or run to completion inline:
 * mmap/mprotect, clock reads, exit, yield, id queries, spawnve (child
 * setup, never a wait) and astia_futex() (the one deliberate
 * synchronization primitive; its uncontended path never sleeps).
 * astia_waitpid is the one direct call that may sleep — halt-and-wake
 * until a child exits.  Everything that must wait on I/O — read, write,
 * open, close, stat, poll, sleep, networking — lives on the async rings
 * in <astia/api/aio.h>.  There are no synchronous read/write syscalls
 * anywhere in the ABI.
 *
 * Return convention: >= 0 on success, or a negative value from
 * <astia/api/errno.h>.  Syscall numbers are the same on every
 * architecture; the table is append-only.
 *
 * Naming: constants are unprefixed — the astia/api/ include path is the
 * namespace — while types and functions keep the astia_ prefix because
 * they live in the global namespace; utility macros keep ASTIA_ because
 * the preprocessor has no namespace.
 *
 * This layer is what a future libc's syscall shims sit on.
 */

#include "wait.h"
#include <astia/api/errno.h>
#include <astia/api/signal_stack.h>
#include <astia/api/signal.h>

#include <stdint.h>

#if defined(__cplusplus)
#if __cplusplus < 201103L
#error "astia/api/syscalls.h requires C++11 or later"
#endif
#elif !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#error "astia/api/syscalls.h requires C11 or later"
#endif

/* utility macros keep the ASTIA_ prefix: the preprocessor has no namespace */
#if defined(__cplusplus)
#define ASTIA_NORETURN [[noreturn]]
#define ASTIA_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define ASTIA_NORETURN _Noreturn
#define ASTIA_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* version of the whole astia userspace ABI */
enum { API_VERSION = 1 };

/* ------------------------------------------------------------------ */
/* syscall numbers — architecture-independent, append-only.            */
/* ------------------------------------------------------------------ */

enum {
    /* ring management; the typed wrappers live in <astia/api/aio.h> */
    SYS_IO_SETUP    = 0,
    SYS_IO_DESTROY  = 1,
    SYS_IO_SUBMIT   = 2,
    SYS_IO_REGISTER = 3,
    /* never-blocking direct calls */
    SYS_MMAP             = 4,
    SYS_MUNMAP           = 5,
    SYS_MPROTECT         = 6,
    SYS_CLOCK_GETTIME    = 7,
    SYS_EXIT             = 8,
    SYS_YIELD            = 9,
    SYS_GETPID           = 10,
    SYS_GETTID           = 11,
    SYS_FUTEX            = 12,
    SYS_WAITPID          = 13,
    SYS_SPAWNVE          = 14,
    SYS_FORK             = 15,
    SYS_THREAD_CREATE    = 16,
    SYS_THREAD_EXIT      = 17,
    SYS_THREAD_JOIN      = 18,
    SYS_THREAD_DETACH    = 19,
    SYS_KILL             = 20,
    SYS_SIGACTION        = 21,
    SYS_SIGPROCMASK      = 22,
    SYS_THREAD_CREATE2   = 23,
    SYS_EXECVE           = 24,
    SYS_SIGSUSPEND       = 25,
    SYS_GETPGRP          = 26,
    SYS_SETPGID          = 27,
    SYS_SETSID           = 28,
    SYS_SIGRETURN        = 29,
    SYS_CHDIR            = 30,
    SYS_GETCWD           = 31,
    SYS_GETUID           = 32,
    SYS_GETEUID          = 33,
    SYS_GETGID           = 34,
    SYS_GETEGID          = 35,
    SYS_SETUID           = 36,
    SYS_SETEUID          = 37,
    SYS_SETGID           = 38,
    SYS_SETEGID          = 39,
    SYS_SETREUID         = 40,
    SYS_SETREGID         = 41,
    SYS_GETGROUPS        = 42,
    SYS_SETGROUPS        = 43,
    SYS_PROCESS_SNAPSHOT = 44,
    SYS_GETSID           = 45,
    SYS_UNMOUNT          = 46,
    SYS_MOUNT            = 47,
    SYS_GETRLIMIT        = 48,
    SYS_SETRLIMIT        = 49,
    SYS_SIGWAIT          = 50,
    SYS_SIGALTSTACK      = 51,
    SYS_GETPGID          = 52,
    SYS_GETPPID          = 53,
    SYS_THREAD_CANCEL    = 54,
    SYS_ALARM            = 55,
    SYS_THREAD_CREATE_OWNED = 56,
    SYS_THREAD_CANCEL_PENDING = 57,
    SYS_THREAD_EXISTS    = 58,
    SYS_THREAD_KILL      = 59,
    SYS_SIGPENDING       = 60,
    SYS_SIGTIMEDWAIT     = 61,
    SYS_CLOCK_GETRES     = 62,
    SYS_THREAD_GETATTR   = 63,
    SYS_MSYNC            = 64,
};

/* ------------------------------------------------------------------ */
/* shared value types                                                  */
/* ------------------------------------------------------------------ */

typedef struct astia_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
} astia_timespec;

typedef struct astia_thread_stack_info {
    uint64_t stack_addr;
    uint64_t stack_size;
    uint32_t detached;
    uint32_t reserved;
} astia_thread_stack_info;

typedef struct astia_rlimit {
    uint64_t current;
    uint64_t maximum;
} astia_rlimit;

enum {
    RLIMIT_AS     = 9,
    RLIMIT_NOFILE = 7,
};

/* One bounded, point-in-time process record.  Names are kernel-owned snapshots;
 * callers must not assume they are paths or remain valid after the call. */
typedef struct astia_process_info {
    int32_t  pid;
    int32_t  ppid;
    int32_t  pgid;
    uint32_t uid;
    uint32_t gid;
    uint32_t state;
    uint32_t has_ctty;
    char     ctty[32];
    char     name[32];
} astia_process_info;

enum {
    ASTIA_PROCESS_FRESH    = 0,
    ASTIA_PROCESS_RUNNABLE = 1,
    ASTIA_PROCESS_STOPPING = 2,
    ASTIA_PROCESS_STOPPED  = 3,
    ASTIA_PROCESS_ZOMBIE   = 4,
    ASTIA_PROCESS_DEAD     = 5,
};
enum { ASTIA_PROCESS_SNAPSHOT_MAX = 64 };

/* ------------------------------------------------------------------ */
/* constants for the direct-call flag words                            */
/* ------------------------------------------------------------------ */

/* mmap prot/flags */
enum {
    PROT_NONE  = 0,
    PROT_READ  = 1u << 0,
    PROT_WRITE = 1u << 1,
    PROT_EXEC  = 1u << 2,
};
enum {
    MAP_SHARED    = 0x01,
    MAP_PRIVATE   = 0x02,
    MAP_FIXED     = 0x10,
    MAP_ANONYMOUS = 0x20, /* fd = -1, offset = 0 */
    MAP_STACK     = 0x20000,
};

enum {
    MS_ASYNC      = 0x01,
    MS_INVALIDATE = 0x02,
    MS_SYNC       = 0x04,
};

/* clocks */
enum {
    CLOCK_MONOTONIC = 0,
    CLOCK_REALTIME  = 1,
    CLOCK_BOOTTIME  = 2,
};

/* futex ops (WAIT may sleep, WAKE never does).  A WAIT timeout is an
 * absolute CLOCK_MONOTONIC astia_timespec. */
enum {
    FUTEX_WAIT         = 0,
    FUTEX_WAKE         = 1,
    FUTEX_PRIVATE_FLAG = 128,
    FUTEX_WAIT_PRIVATE = 128,
    FUTEX_WAKE_PRIVATE = 129,
    FUTEX_CANCELABLE_FLAG = 256,
    FUTEX_WAIT_CANCELABLE_PRIVATE = 384,
};

/* standard streams */
enum { STDIN = 0, STDOUT = 1, STDERR = 2 };

/* ------------------------------------------------------------------ */
/* raw trap                                                            */
/*                                                                     */
/* Argument order follows the x86_64 syscall convention                */
/* (rdi, rsi, rdx, r10, r8, r9); aarch64 uses x0..x5.                  */
/* ------------------------------------------------------------------ */

#if defined(__x86_64__)

static inline long astia_raw6(long nr, long a0, long a1, long a2, long a3, long a4,
                              long a5) {
    register long r10 __asm__("r10") = a3;
    register long r8 __asm__("r8")   = a4;
    register long r9 __asm__("r9")   = a5;
    long          ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a0), "S"(a1), "d"(a2), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}

#elif defined(__aarch64__)

static inline long astia_raw6(long nr, long a0, long a1, long a2, long a3, long a4,
                              long a5) {
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    register long x4 __asm__("x4") = a4;
    register long x5 __asm__("x5") = a5;
    register long x8 __asm__("x8") = nr;
    __asm__ volatile("svc 0"
                     : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
                     : "memory", "cc");
    return x0;
}

#else

/* Unknown architecture: the port provides the trap. */
long astia_raw6(long nr, long a0, long a1, long a2, long a3, long a4, long a5);

#endif

/* ------------------------------------------------------------------ */
/* typed wrappers for the direct (never-blocking) calls                */
/* ------------------------------------------------------------------ */

static inline long astia_mmap(void* addr, uint64_t len, uint32_t prot, uint32_t flags,
                              int32_t fd, int64_t off) {
    return astia_raw6(SYS_MMAP, (long) addr, (long) len, (long) prot, (long) flags,
                      (long) fd, (long) off);
}

static inline long astia_munmap(void* addr, uint64_t len) {
    return astia_raw6(SYS_MUNMAP, (long) addr, (long) len, 0, 0, 0, 0);
}

static inline long astia_mprotect(void* addr, uint64_t len, uint32_t prot) {
    return astia_raw6(SYS_MPROTECT, (long) addr, (long) len, (long) prot, 0, 0, 0);
}

static inline long astia_msync(void* addr, uint64_t len, uint32_t flags) {
    return astia_raw6(SYS_MSYNC, (long) addr, (long) len, (long) flags, 0, 0, 0);
}

static inline long astia_getrlimit(int32_t resource, astia_rlimit* limit) {
    return astia_raw6(SYS_GETRLIMIT, (long) resource, (long) limit, 0, 0, 0, 0);
}

static inline long astia_setrlimit(int32_t resource, const astia_rlimit* limit) {
    return astia_raw6(SYS_SETRLIMIT, (long) resource, (long) limit, 0, 0, 0, 0);
}

static inline long astia_clock_gettime(uint32_t clock_id, astia_timespec* out) {
    return astia_raw6(SYS_CLOCK_GETTIME, (long) clock_id, (long) out, 0, 0, 0, 0);
}

static inline long astia_clock_getres(uint32_t clock_id, astia_timespec* out) {
    return astia_raw6(SYS_CLOCK_GETRES, (long) clock_id, (long) out, 0, 0, 0, 0);
}

ASTIA_NORETURN static inline void astia_exit(int32_t status) {
    (void) astia_raw6(SYS_EXIT, (long) status, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

static inline long astia_yield(void) {
    return astia_raw6(SYS_YIELD, 0, 0, 0, 0, 0, 0);
}

static inline long astia_getpid(void) {
    return astia_raw6(SYS_GETPID, 0, 0, 0, 0, 0, 0);
}

static inline long astia_getppid(void) {
    return astia_raw6(SYS_GETPPID, 0, 0, 0, 0, 0, 0);
}

static inline long astia_gettid(void) {
    return astia_raw6(SYS_GETTID, 0, 0, 0, 0, 0, 0);
}
static inline long astia_sigaltstack(const astia_stack_t* stack, astia_stack_t* old_stack) {
    return astia_raw6(SYS_SIGALTSTACK, (long) stack, (long) old_stack, 0, 0, 0, 0);
}
static inline long astia_unmount(const char* target) {
    return astia_raw6(SYS_UNMOUNT, (long) target, 0, 0, 0, 0, 0);
}
static inline long astia_mount(const char* type, const char* target, const char* source) {
    return astia_raw6(SYS_MOUNT, (long) type, (long) target, (long) source, 0, 0, 0);
}
static inline long astia_getuid(void) {
    return astia_raw6(SYS_GETUID, 0, 0, 0, 0, 0, 0);
}
static inline long astia_geteuid(void) {
    return astia_raw6(SYS_GETEUID, 0, 0, 0, 0, 0, 0);
}
static inline long astia_getgid(void) {
    return astia_raw6(SYS_GETGID, 0, 0, 0, 0, 0, 0);
}
static inline long astia_getegid(void) {
    return astia_raw6(SYS_GETEGID, 0, 0, 0, 0, 0, 0);
}
static inline long astia_setuid(uint32_t v) {
    return astia_raw6(SYS_SETUID, v, 0, 0, 0, 0, 0);
}
static inline long astia_seteuid(uint32_t v) {
    return astia_raw6(SYS_SETEUID, v, 0, 0, 0, 0, 0);
}
static inline long astia_setgid(uint32_t v) {
    return astia_raw6(SYS_SETGID, v, 0, 0, 0, 0, 0);
}
static inline long astia_setegid(uint32_t v) {
    return astia_raw6(SYS_SETEGID, v, 0, 0, 0, 0, 0);
}
static inline long astia_setreuid(uint32_t r, uint32_t e) {
    return astia_raw6(SYS_SETREUID, r, e, 0, 0, 0, 0);
}
static inline long astia_setregid(uint32_t r, uint32_t e) {
    return astia_raw6(SYS_SETREGID, r, e, 0, 0, 0, 0);
}
static inline long astia_getgroups(int32_t n, uint32_t* g) {
    return astia_raw6(SYS_GETGROUPS, n, (long) g, 0, 0, 0, 0);
}
static inline long astia_setgroups(int32_t n, const uint32_t* g) {
    return astia_raw6(SYS_SETGROUPS, n, (long) g, 0, 0, 0, 0);
}
static inline long astia_process_snapshot(astia_process_info* out, uint32_t capacity) {
    return astia_raw6(SYS_PROCESS_SNAPSHOT, (long) out, (long) capacity, 0, 0, 0, 0);
}

static inline long astia_getpgrp(void) {
    return astia_raw6(SYS_GETPGRP, 0, 0, 0, 0, 0, 0);
}

static inline long astia_getpgid(int32_t pid) {
    return astia_raw6(SYS_GETPGID, (long) pid, 0, 0, 0, 0, 0);
}

static inline long astia_setpgid(int32_t pid, int32_t pgid) {
    return astia_raw6(SYS_SETPGID, (long) pid, (long) pgid, 0, 0, 0, 0);
}

static inline long astia_setsid(void) {
    return astia_raw6(SYS_SETSID, 0, 0, 0, 0, 0, 0);
}
/* pid == 0 selects the caller; a positive PID selects that process. */
static inline long astia_getsid(int32_t pid) {
    return astia_raw6(SYS_GETSID, (long) pid, 0, 0, 0, 0, 0);
}

static inline long astia_alarm(uint32_t seconds) {
    return astia_raw6(SYS_ALARM, (long) seconds, 0, 0, 0, 0, 0);
}

static inline long astia_kill(int32_t pid, int32_t signal) {
    return astia_raw6(SYS_KILL, (long) pid, (long) signal, 0, 0, 0, 0);
}

static inline long astia_sigaction(int32_t signal, const astia_sigaction_t* next,
                                   astia_sigaction_t* old) {
    return astia_raw6(SYS_SIGACTION, (long) signal, (long) next, (long) old, 0, 0, 0);
}

static inline long astia_sigprocmask(int32_t how, uint32_t set, uint32_t* old) {
    return astia_raw6(SYS_SIGPROCMASK, (long) how, (long) set, (long) old, 0, 0, 0);
}

static inline long astia_sigwait(const uint32_t* set, int32_t* signal) {
    return astia_raw6(SYS_SIGWAIT, (long) set, (long) signal, 0, 0, 0, 0);
}

static inline long astia_sigtimedwait(const uint32_t* set, astia_siginfo_t* info,
                                      const astia_timespec* timeout) {
    return astia_raw6(SYS_SIGTIMEDWAIT, (long) set, (long) info, (long) timeout,
                      0, 0, 0);
}

/* pid > 0: wait for that child; pid == -1: any child.  On success the
 * reaped pid is returned and *status (if non-null) receives the exit
 * status.  May block. */
static inline long astia_waitpid_options(int32_t pid, int32_t* status, uint32_t options) {
    return astia_raw6(SYS_WAITPID, (long) pid, (long) status, (long) options, 0, 0, 0);
}
static inline long astia_waitpid(int32_t pid, int32_t* status) {
    return astia_waitpid_options(pid, status, 0);
}

static inline long astia_chdir(const char* path) {
    return astia_raw6(SYS_CHDIR, (long) path, 0, 0, 0, 0, 0);
}
static inline long astia_getcwd(char* buffer, uint64_t length) {
    return astia_raw6(SYS_GETCWD, (long) buffer, (long) length, 0, 0, 0, 0);
}

/* Create a process from the executable at `path`, materializing the
 * NULL-terminated argv/envp vectors on its stack (a null vector means
 * empty).  Returns the child pid > 0, or a negative error code.
 * Never waits: pair it with astia_waitpid. */
static inline long astia_spawnve(const char* path, char* const argv[],
                                 char* const envp[]) {
    return astia_raw6(SYS_SPAWNVE, (long) path, (long) argv, (long) envp, 0, 0, 0);
}

/* Duplicate the calling address space copy-on-write.  Returns 0 in the
 * child — which resumes exactly where the parent called, with rax = 0
 * — and the child's pid in the parent; negative error code otherwise.
 * Every mapping is shared until either side writes it. */
static inline long astia_fork(void) {
    return astia_raw6(SYS_FORK, 0, 0, 0, 0, 0, 0);
}

static inline long astia_futex(uint64_t* addr, uint32_t op, uint32_t val,
                               const astia_timespec* timeout, uint32_t val2,
                               void* addr2) {
    return astia_raw6(SYS_FUTEX, (long) addr, (long) op, (long) val, (long) timeout,
                      (long) val2, (long) addr2);
}

static inline long astia_thread_create(void (*entry)(void*), void* stack_top,
                                       void* argument) {
    return astia_raw6(SYS_THREAD_CREATE, (long) (uintptr_t) entry, (long) stack_top,
                      (long) argument, 0, 0, 0);
}

static inline long astia_thread_create2(void (*entry)(void*, void*), void* stack_top,
                                        void* argument0, void* argument1,
                                        void* stack_addr, uint64_t stack_size) {
    return astia_raw6(SYS_THREAD_CREATE2, (long) (uintptr_t) entry, (long) stack_top,
                      (long) argument0, (long) argument1, (long) stack_addr,
                      (long) stack_size);
}

static inline long astia_thread_create_owned(void (*entry)(void*, void*), void* stack_top,
                                             void* argument0, void* argument1,
                                             void* stack_addr, uint64_t stack_size) {
    return astia_raw6(SYS_THREAD_CREATE_OWNED, (long) (uintptr_t) entry,
                      (long) stack_top, (long) argument0, (long) argument1,
                      (long) stack_addr, (long) stack_size);
}

static inline long astia_thread_getattr(uint32_t tid, astia_thread_stack_info* output) {
    return astia_raw6(SYS_THREAD_GETATTR, (long) tid, (long) output, 0, 0, 0, 0);
}

static inline long astia_thread_cancel(uint32_t tid) {
    return astia_raw6(SYS_THREAD_CANCEL, (long) tid, 0, 0, 0, 0, 0);
}

static inline long astia_thread_cancel_pending(void) {
    return astia_raw6(SYS_THREAD_CANCEL_PENDING, 0, 0, 0, 0, 0, 0);
}

static inline long astia_thread_exists(uint32_t tid) {
    return astia_raw6(SYS_THREAD_EXISTS, (long) tid, 0, 0, 0, 0, 0);
}
static inline long astia_thread_kill(uint32_t tid, int32_t signal) {
    return astia_raw6(SYS_THREAD_KILL, (long) tid, (long) signal, 0, 0, 0, 0);
}
static inline long astia_sigpending(uint32_t* set) {
    return astia_raw6(SYS_SIGPENDING, (long) set, 0, 0, 0, 0, 0);
}

static inline void astia_thread_exit(void* result) {
    (void) astia_raw6(SYS_THREAD_EXIT, (long) result, 0, 0, 0, 0, 0);
    for (;;) {
    }
}

static inline long astia_thread_join(uint32_t tid, void** result) {
    return astia_raw6(SYS_THREAD_JOIN, (long) tid, (long) result, 0, 0, 0, 0);
}

static inline long astia_thread_detach(uint32_t tid) {
    return astia_raw6(SYS_THREAD_DETACH, (long) tid, 0, 0, 0, 0, 0);
}

#ifdef __cplusplus
} /* extern "C" */
#endif
