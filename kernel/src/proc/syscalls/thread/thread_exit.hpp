#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// thread_exit - Terminate the calling user thread.
    ///
    /// Synopsis:
    ///     long thread_exit(long result);
    ///
    /// Description:
    ///     Terminates the calling thread and records result for a joining
    ///     thread. A process becomes waitable after its last thread exits.
    ///
    /// Return Value:
    ///     Does not return.
    i64 thread_exit(i64 result);
}
