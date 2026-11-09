#pragma once

#include <astia/api/signal_stack.h>

#include <stdint.h>

/* User-facing signal disposition ABI. */
typedef struct astia_sigaction {
    uintptr_t handler; /* ASTIA_SIG_DFL, ASTIA_SIG_IGN, or function address */
    uint32_t  mask;
    uint32_t  flags;
    uintptr_t restorer;
} astia_sigaction_t;

typedef struct astia_siginfo {
    int32_t  signo;
    int32_t  errno_value;
    int32_t  code;
    int32_t  pid;
    uint32_t uid;
    int32_t  status;
} astia_siginfo_t;

typedef struct astia_x86_64_signal_context {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15, rbp;
    uint64_t rip, rsp, rflags;
    uint32_t blocked;
    uint32_t reserved;
} astia_x86_64_signal_context_t;

#define ASTIA_SIG_DFL ((uintptr_t) 0)
#define ASTIA_SIG_IGN ((uintptr_t) 1)
#define ASTIA_SA_ONSTACK 1
#define ASTIA_SA_SIGINFO 4
#define ASTIA_SA_RESTART 0x10000000
#define ASTIA_SA_NODEFER 0x40000000
#define ASTIA_SA_RESETHAND 0x80000000u

#define ASTIA_CLD_EXITED    1
#define ASTIA_CLD_KILLED    2
#define ASTIA_CLD_DUMPED    3
#define ASTIA_CLD_STOPPED   5
#define ASTIA_CLD_CONTINUED 6
#define ASTIA_SI_USER       0

#define ASTIA_SIGAIO 29
enum {
    ASTIA_SIG_BLOCK   = 0,
    ASTIA_SIG_UNBLOCK = 1,
    ASTIA_SIG_SETMASK = 2,
};
