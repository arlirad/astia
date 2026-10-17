#pragma once

#include <stdint.h>

/* User-facing signal disposition ABI. */
typedef struct astia_sigaction {
    uintptr_t handler; /* ASTIA_SIG_DFL, ASTIA_SIG_IGN, or function address */
    uint32_t  mask;
    uint32_t  flags;
} astia_sigaction_t;

#define ASTIA_SIG_DFL ((uintptr_t) 0)
#define ASTIA_SIG_IGN ((uintptr_t) 1)

enum {
    ASTIA_SIG_BLOCK   = 0,
    ASTIA_SIG_UNBLOCK = 1,
    ASTIA_SIG_SETMASK = 2,
};
