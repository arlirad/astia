#pragma once

#include <astia/arch/interrupt_frame.hpp>
#include <astia/dbg/panic_report.hpp>

namespace astia::arch::x86_64 {
    struct panic_state {
        interrupt_frame frame{};
        bool            fault = false;
        u64             cr0   = 0;
        u64             cr2   = 0;
        u64             cr3   = 0;
        u64             cr4   = 0;
    };

    /// Pure register/fault formatter, kept separate from capture so host
    /// tests can feed synthetic frames.
    void format_panic_registers(const panic_state& state, dbg::sink_fn sink,
                                void* context);
} // namespace astia::arch::x86_64
