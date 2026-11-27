#pragma once

#include <astia/common.hpp>

// AArch64 kernel execution context for the boot-CPU scheduler stage.
//
// The saved set is the AAPCS64 callee-saved general-register subset
// (x19-x28, x29/FP, x30/LR) plus the stack pointer and PSTATE.DAIF, following
// Arm DDI 0487: only callee-saved registers survive a normal call, so a
// cooperative switch only has to preserve those.  FP/SIMD state is not part
// of the context: the kernel is built with -mgeneral-regs-only and no kernel
// path executes an FP/SIMD instruction, so there is no state to preserve.
// Any future FP/SIMD use in kernel context must either save q0-q31/fpsr/fpcr
// here or leave CPACR_EL1.FPEN trapping kernel access.
namespace astia::arch {
    struct context {
        u64 x19;
        u64 x20;
        u64 x21;
        u64 x22;
        u64 x23;
        u64 x24;
        u64 x25;
        u64 x26;
        u64 x27;
        u64 x28;
        u64 fp;
        u64 lr;
        u64 sp;
        u64 daif;
    };

    static_assert(sizeof(context) == 112);
    static_assert(__builtin_offsetof(context, fp) == 80);
    static_assert(__builtin_offsetof(context, lr) == 88);
    static_assert(__builtin_offsetof(context, sp) == 96);
    static_assert(__builtin_offsetof(context, daif) == 104);

    /// Switch from `old_context` to `next_context`.  Declared here instead
    /// of pulling in <astia/arch/context.hpp>: the generic header adds the
    /// user-context surface and a <concepts> dependency the freestanding
    /// AArch64 port does not need.
    extern "C" void context_switch(context* old_context, const context* next_context);

    /// Prepare a context that enters `entry(argument)` on `stack_top`.
    /// The context starts with DAIF masked; the scheduler opens the gate
    /// when the new context is first selected.
    bool context_prepare_kernel_entry(context* value, u64 stack_top, u64 entry,
                                      u64 argument);
} // namespace astia::arch
