#pragma once

#include <astia/common.hpp>

namespace astia::arch::x86_64 {

    struct syscall_frame {
        u64 nr, a0, a1, a2, a3, a4, a5;
        u64 rip, rflags, rsp;
        // Callee-saved image: the entry stores it so the exit path can
        // restore it explicitly (no reliance on the compiler's choices
        // in the C++ handler), and fork packs the child's resume from
        // it — the one call that needs the whole register set.
        u64 rbx, rbp, r12, r13, r14, r15;
    };

} // namespace astia::arch::x86_64
