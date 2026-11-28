#pragma once

#include "../../../kernel/include/astia/api/syscalls.h"

#include <stdint.h>

/* Unknown states fall back to '?'.  No ps(1) modifier is emitted because the
 * snapshot carries no session, foreground-group, or thread information. */
static inline char cu_ps_state_letter(uint32_t state) {
    switch (state) {
    case ASTIA_PROCESS_FRESH:
        return 'F';
    case ASTIA_PROCESS_RUNNABLE:
        return 'R';
    case ASTIA_PROCESS_STOPPING:
        return 'T';
    case ASTIA_PROCESS_STOPPED:
        return 'T';
    case ASTIA_PROCESS_ZOMBIE:
        return 'Z';
    case ASTIA_PROCESS_DEAD:
        return 'X';
    default:
        return '?';
    }
}
