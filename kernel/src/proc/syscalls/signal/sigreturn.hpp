#pragma once
#include <astia/common.hpp>
#include <astia/arch/context.hpp>
namespace astia::proc::syscall {
    /// sigreturn - restore a saved signal execution context.
    ///
    /// Synopsis:
    ///     long sigreturn(const signal_context *context);
    ///
    /// Errors:
    ///     -EFAULT: The saved context or its registers are invalid.
    i64 sigreturn(const arch::syscall_frame& input);
}
