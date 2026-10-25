#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// thread_create2 - Create a user thread with two arguments.
    ///
    /// Synopsis:
    ///     long thread_create2(void (*entry)(void *, void *), void *stack_top, void *argument0, void *argument1);
    ///
    /// Description:
    ///     Creates a thread and passes both arguments directly to entry on the
    ///     supplied aligned user stack.
    ///
    /// Return Value:
    ///     Returns the new thread ID or a negative errno.
    ///
    /// Errors:
    ///     -EAGAIN: The thread limit or memory limit was reached.
    ///     -EFAULT: The entry point, stack, or argument pointer is invalid.
    ///     -ESRCH: There is no current process.
    i64 thread_create2(u64 entry, u64 stack_top, u64 argument0, u64 argument1);
}
