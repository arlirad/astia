#pragma once

// Canonical per-CPU anchor addressed through %gs.  The syscall entry path
// reads index, syscall_stack_top, and syscall_saved_r12 before it has a C
// frame in which to recover a CPU-local pointer, so these offsets are part
// of the ABI between syscall.S, isrs.S, and cpu.cpp.  The macros below are
// the single source of truth: the struct asserts its fields sit at exactly
// those offsets, and the assembly files use the macros directly after
// preprocessing.
//
// This header is included from .S files, so only the macros are visible
// there; the C++ view is guarded.

#define ASTIA_GS_CPU_INDEX 0
#define ASTIA_GS_SYSCALL_STACK_TOP 8
#define ASTIA_GS_SYSCALL_SAVED_R12 16
#define ASTIA_GS_INTERRUPT_DEPTH 24
#define ASTIA_GS_SHOULD_YIELD 28

#ifdef __cplusplus

#include <astia/common.hpp>

#include <cstddef>

namespace astia::arch::x86_64 {
    struct alignas(astia::cache_line_bytes) cpu_local {
        u32 index;
        u32 reserved;
        u64 syscall_stack_top;
        u64 syscall_saved_r12;
        u32 interrupt_depth;
        u32 should_yield;
    };

    static_assert(offsetof(cpu_local, index) == ASTIA_GS_CPU_INDEX);
    static_assert(offsetof(cpu_local, reserved) == 4);
    static_assert(offsetof(cpu_local, syscall_stack_top) == ASTIA_GS_SYSCALL_STACK_TOP);
    static_assert(offsetof(cpu_local, syscall_saved_r12) == ASTIA_GS_SYSCALL_SAVED_R12);
    static_assert(offsetof(cpu_local, interrupt_depth) == ASTIA_GS_INTERRUPT_DEPTH);
    static_assert(offsetof(cpu_local, should_yield) == ASTIA_GS_SHOULD_YIELD);
} // namespace astia::arch::x86_64

#endif // __cplusplus
