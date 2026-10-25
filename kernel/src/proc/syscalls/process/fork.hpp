#pragma once
#include <astia/common.hpp>
#include <astia/arch/context.hpp>
namespace astia::proc::syscall {
    /// fork - Create a copy-on-write child process.
    ///
    /// Synopsis:
    ///     long fork(void);
    ///
    /// Description:
    ///     Creates a child sharing copy-on-write mappings with the parent.
    ///     Descriptors and the current working directory are inherited.
    ///
    /// Return Value:
    ///     Returns the child PID in the parent and zero in the child.
    ///
    /// Errors:
    ///     -EAGAIN: A process, thread, mapping, or registry slot cannot be allocated.
    ///     -ESRCH: There is no current user process.
    i64 fork(const arch::syscall_frame& frame);
}
