#pragma once

#include <astia/common.hpp>

namespace astia::arch {

    /// The register/iret image the x86-64 ISR stubs push.  The asm owns the
    /// layout; this definition is port-private so generic code only sees an
    /// opaque frame handle.
    struct interrupt_frame {
        u64 r15, r14, r13, r12, r11, r10, r9, r8;
        u64 rdi, rsi, rbp, rbx, rdx, rcx, rax;
        u64 vector, error;
        u64 rip, cs, rflags, rsp, ss;
    };

} // namespace astia::arch
