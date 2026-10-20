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

#include <astia/api/errno.h>
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
    SYS_IO_ENTER    = 2,
    SYS_IO_REGISTER = 3,
    /* never-blocking direct calls */
    SYS_MMAP          = 4,
    SYS_MUNMAP        = 5,
    SYS_MPROTECT      = 6,
    SYS_CLOCK_GETTIME = 7,
    SYS_EXIT          = 8,
    SYS_YIELD         = 9,
    SYS_GETPID        = 10,
    SYS_GETTID        = 11,
    SYS_FUTEX         = 12,
    SYS_WAITPID       = 13,
    SYS_SPAWNVE       = 14,
    SYS_FORK          = 15,
    SYS_THREAD_CREATE = 16,
    SYS_THREAD_EXIT   = 17,
    SYS_THREAD_JOIN   = 18,
    SYS_THREAD_DETACH = 19,
    SYS_KILL          = 20,
    SYS_SIGACTION     = 21,
    SYS_SIGPROCMASK   = 22,
    SYS_THREAD_CREATE2 = 23,
    SYS_EXECVE         = 24,
    SYS_SIGSUSPEND     = 25,
    SYS_GETPGRP        = 26,
    SYS_SETPGID        = 27,
    SYS_SETSID         = 28,
    SYS_SIGRETURN      = 29,
};

/* ------------------------------------------------------------------ */
/* shared value types                                                  */
/* ------------------------------------------------------------------ */

typedef struct astia_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
} astia_timespec;

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
};

/* clocks */
enum {
    CLOCK_MONOTONIC = 0,
    CLOCK_REALTIME  = 1,
    CLOCK_BOOTTIME  = 2,
};

/* futex ops (WAIT may sleep, WAKE never does) */
enum {
    FUTEX_WAIT         = 0,
    FUTEX_WAKE         = 1,
    FUTEX_PRIVATE_FLAG = 128,
    FUTEX_WAIT_PRIVATE = 128,
    FUTEX_WAKE_PRIVATE = 129,
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

static inline long astia_clock_gettime(uint32_t clock_id, astia_timespec* out) {
    return astia_raw6(SYS_CLOCK_GETTIME, (long) clock_id, (long) out, 0, 0, 0, 0);
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

static inline long astia_gettid(void) {
    return astia_raw6(SYS_GETTID, 0, 0, 0, 0, 0, 0);
}

static inline long astia_getpgrp(void) {
    return astia_raw6(SYS_GETPGRP, 0, 0, 0, 0, 0, 0);
}

static inline long astia_setpgid(int32_t pid, int32_t pgid) {
    return astia_raw6(SYS_SETPGID, (long) pid, (long) pgid, 0, 0, 0, 0);
}

static inline long astia_setsid(void) {
    return astia_raw6(SYS_SETSID, 0, 0, 0, 0, 0, 0);
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

/* pid > 0: wait for that child; pid == -1: any child.  On success the
 * reaped pid is returned and *status (if non-null) receives the exit
 * status.  May block. */
enum { ASTIA_WNOHANG = 1u << 0, ASTIA_WUNTRACED = 1u << 1,
       ASTIA_WCONTINUED = 1u << 3 };
static inline long astia_waitpid_options(int32_t pid, int32_t* status, uint32_t options) {
    return astia_raw6(SYS_WAITPID, (long) pid, (long) status, (long) options, 0, 0, 0);
}
static inline long astia_waitpid(int32_t pid, int32_t* status) {
    return astia_waitpid_options(pid, status, 0);
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
                                        void* argument0, void* argument1) {
    return astia_raw6(SYS_THREAD_CREATE2, (long) (uintptr_t) entry,
                      (long) stack_top, (long) argument0, (long) argument1, 0, 0);
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
