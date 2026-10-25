#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// thread_create - Create a user thread.
    ///
    /// Synopsis:
    ///     long thread_create(void (*entry)(void *), void *stack_top, void *argument);
    ///
    /// Description:
    ///     Creates a thread in the current process and starts entry with
    ///     argument on the supplied aligned user stack.
    ///
    /// Return Value:
    ///     Returns the new thread ID or a negative errno.
    ///
    /// Errors:
    ///     -EAGAIN: The thread limit or memory limit was reached.
    ///     -EFAULT: The entry point or user stack is invalid.
    ///     -ESRCH: There is no current process.
    i64 thread_create(u64 entry, u64 stack_top, u64 argument);
}
