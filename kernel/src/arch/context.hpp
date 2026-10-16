#pragma once

#include <astia/common.hpp>

namespace astia::arch {

    /// Opaque architecture-owned execution context.  The selected
    /// architecture defines its register layout and allocates it.
    struct context;

    /// Register image of a ring-3 caller at syscall time.  The arch
    /// syscall entry packs it from its frame; fork hands the copy to
    /// context_prepare_fork so the child resumes exactly where the
    /// parent sat.  Callee-saved registers plus the ring-3 control
    /// state are the full contract — the remaining caller-saved
    /// registers follow the syscall ABI (clobbered, zeroed on resume).
    struct user_context {
        u64 rip     = 0;
        u64 rflags  = 0;
        u64 rsp     = 0;
        u64 rbx     = 0;
        u64 rbp     = 0;
        u64 r12     = 0;
        u64 r13     = 0;
        u64 r14     = 0;
        u64 r15     = 0;
        u64 fs_base = 0; ///< thread pointer the resume loads into %fs
    };

    auto context_create() -> context*;
    auto context_prepare_kernel(context*, u64 stack_top, u64 entry) -> bool;
    auto context_prepare_user(context*, u64 kernel_stack_top, u64 user_stack_top,
                              u64 entry, u64 fs_base) -> bool;
    /// Prepare a new user thread at `entry` with its first argument in
    /// the ABI's %rdi.  The architecture uses a callee-saved staging
    /// register while entering the synthetic iretq frame.
    auto context_prepare_user_start(context*, u64 kernel_stack_top, u64 user_stack_top,
                                    u64 entry, u64 fs_base, u64 argument) -> bool;
    /// Prepare `value` to enter ring 3 exactly where a forked parent
    /// sat inside its syscall: the register image, thread pointer, and
    /// FP/SSE state are the parent's, and rax reads 0 — the child's
    /// fork() return value.  The caller supplies the kernel stack the
    /// resume trampoline builds its iretq frame on.
    auto context_prepare_fork(context*, u64 kernel_stack_top, const user_context* image)
        -> bool;
    auto context_destroy(context*) -> void;

    extern "C" auto context_switch(context* old_context, const context* next_context)
        -> void;

} // namespace astia::arch
