#pragma once

#include <stdint.h>

typedef struct astia_stack {
    uintptr_t sp;
    uint64_t  size;
    int32_t   flags;
    int32_t   reserved;
} astia_stack_t;

#define ASTIA_SS_ONSTACK 1
#define ASTIA_SS_DISABLE 2
#define ASTIA_MINSIGSTKSZ 2048
