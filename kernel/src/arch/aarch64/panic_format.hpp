#pragma once

#include <astia/arch/exception_frame.hpp>
#include <astia/dbg/panic_report.hpp>

namespace astia::arch::aarch64 {
    /// Pure register/fault formatter for a saved exception frame.
    void format_panic_registers(const exception_frame& frame, u64 interrupted_sp,
                                u64 ttbr0, u64 vector_kind, dbg::sink_fn sink,
                                void* context);
} // namespace astia::arch::aarch64
