#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// yield - Yield the current scheduling turn.
    ///
    /// Synopsis:
    ///     long yield(void);
    ///
    /// Description:
    ///     Gives another runnable thread an opportunity to run. The caller
    ///     remains runnable and may be selected again immediately.
    ///
    /// Return Value:
    ///     Returns zero.
    i64 yield();
}
