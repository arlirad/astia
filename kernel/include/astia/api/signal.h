#pragma once

#include <stdint.h>

/* User-facing signal disposition ABI. */
typedef struct astia_sigaction {
    uintptr_t handler; /* ASTIA_SIG_DFL, ASTIA_SIG_IGN, or function address */
    uint32_t  mask;
    uint32_t  flags;
    uintptr_t restorer;
} astia_sigaction_t;

typedef struct astia_x86_64_signal_context {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15, rbp;
    uint64_t rip, rsp, rflags;
    uint32_t blocked;
    uint32_t reserved;
} astia_x86_64_signal_context_t;

#define ASTIA_SIG_DFL ((uintptr_t) 0)
#define ASTIA_SIG_IGN ((uintptr_t) 1)

#define ASTIA_SIGAIO 29

enum {
    ASTIA_SIG_BLOCK   = 0,
    ASTIA_SIG_UNBLOCK = 1,
    ASTIA_SIG_SETMASK = 2,
};
