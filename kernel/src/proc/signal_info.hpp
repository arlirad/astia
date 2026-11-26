#pragma once

#include <astia/api/signal.h>
#include <astia/common.hpp>
#include <astia/memory.hpp>

namespace astia::proc {
    /// Build the userspace signal-info record from a zeroed object image so
    /// no uninitialized storage (including any ABI padding) reaches copy_out.
    inline astia_siginfo_t make_siginfo(i32 signo, i32 errno_value, i32 code, i32 pid,
                                        u32 uid, i32 status, u64 addr) {
        astia_siginfo_t value;

        fill_zero(&value, sizeof value);

        value.signo       = signo;
        value.errno_value = errno_value;
        value.code        = code;
        value.pid         = pid;
        value.uid         = uid;
        value.status      = status;
        value.addr        = static_cast<uintptr_t>(addr);

        return value;
    }
} // namespace astia::proc
