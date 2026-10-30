#pragma once
#include <astia/arch/context.hpp>
#include <astia/common.hpp>
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
