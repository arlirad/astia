#pragma once

#include <astia/common.hpp>

namespace astia::arch {

    /// Opaque architecture-owned execution context.  The selected
    /// architecture defines its register layout and allocates it.
    struct context;
    void context_set_fs_base(context* value, u64 fs_base);

    /// Return-side register image of a ring-3 caller.  The syscall number
    /// and argument registers are not part of it: the entry hands those to
    /// the dispatcher directly.  This image carries only what the exit
    /// path must materialize — the return mode, the caller-saved
    /// registers sigreturn restores, the ring-3 control state, and the
    /// callee-saved image the exit path restores explicitly.  The selected
    /// architecture defines the layout and its syscall entry is the one
    /// writer — nothing else may reorder it.
    struct syscall_return {
        /// How the exit path returns to ring 3.  Kept at offset zero because
        /// the entry tests it there; the architecture decides which register
        /// materializes it.  The syscall result is not stored here.
        u64 mode;
        // Caller-saved image.  Not syscall arguments: signal delivery saves
        // them for the interrupted context and sigreturn restores them.
        u64 rdi, rsi, rdx, r10, r8, r9;
        u64 rip, rflags, rsp;
        // Callee-saved image: the entry stores it so the exit path can
        // restore it explicitly (no reliance on the compiler's choices
        // in the C++ handler), and fork packs the child's resume from
        // it — the one call that needs the whole register set.
        u64 rbx, rbp, r12, r13, r14, r15;
    };

    /// Values stored in `syscall_return::mode`.  These are return paths, not
    /// error codes: the ordinary result travels separately in the return
    /// register the C door gives back to the entry.
    namespace return_mode {
        /// Restore the interrupted user context as-is.
        inline constexpr u64 ordinary = 0;
        /// Deliver a pending signal before returning to ring 3.
        inline constexpr u64 signal = 1;
        /// Restore the saved image from sigreturn.
        inline constexpr u64 sigreturn = 2;
    } // namespace return_mode

    /// Register image of a ring-3 caller at syscall time.  The arch
    /// syscall entry packs it from its frame; fork hands the copy to
    /// context_prepare_fork so the child resumes exactly where the
    /// parent sat.  Callee-saved registers plus the ring-3 control state are
    /// the full contract; syscall-preserved argument registers are retained.
    struct user_context {
        u64 rip    = 0;
        u64 rflags = 0;
        u64 rsp    = 0;
        u64 rbx    = 0;
        u64 rbp    = 0;
        u64 r12    = 0;
        u64 r13    = 0;
        u64 r14    = 0;
        u64 r15    = 0;
        // Registers preserved by the syscall ABI and needed by the caller
        // immediately after fork returns.
        u64 rdi     = 0;
        u64 rsi     = 0;
        u64 rdx     = 0;
        u64 r10     = 0;
        u64 r8      = 0;
        u64 r9      = 0;
        u64 fs_base = 0; ///< thread pointer the resume loads into %fs
    };

    /// The caller's own state out of a syscall return image and the six
    /// argument registers captured at entry, in the form a context is
    /// prepared from: what fork hands the child so it returns from the
    /// syscall exactly where the parent sat.  It deliberately does not
    /// carry fs_base — the thread pointer belongs to the address space,
    /// not to the trap, and proc::fork takes it from the parent's.
    user_context caller_image(const syscall_return& image, u64 rdi, u64 rsi, u64 rdx,
                              u64 r10, u64 r8, u64 r9);

    context* context_create();
    bool     context_prepare_kernel(context*, u64 stack_top, u64 entry);
    bool     context_prepare_user(context*, u64 kernel_stack_top, u64 user_stack_top,
                                  u64 entry, u64 fs_base);
    /// Prepare a new user thread at `entry` with its first argument in
    /// the ABI's %rdi.  The architecture uses a callee-saved staging
    /// register while entering the synthetic iretq frame.
    bool context_prepare_user_start(context*, u64 kernel_stack_top, u64 user_stack_top,
                                    u64 entry, u64 fs_base, u64 argument0, u64 argument1);
    /// Prepare `value` to enter ring 3 exactly where a forked parent
    /// sat inside its syscall: the register image, thread pointer, and
    /// FP/SSE state are the parent's, and the child's fork() return value
    /// reads 0.  The caller supplies the kernel stack the
    /// resume trampoline builds its iretq frame on.
    bool context_prepare_fork(context*, u64 kernel_stack_top, const user_context* image);
    void context_destroy(context*);

    extern "C" void context_switch(context* old_context, const context* next_context);

} // namespace astia::arch
