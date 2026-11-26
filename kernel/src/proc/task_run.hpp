#pragma once

#include <astia/core/task.hpp>

namespace astia::proc {
    /// Run a task on the current thread and park until it completes.
    /// The async syscall dispatcher and kernel boot/selftest drivers use
    /// this as the blocking boundary for coroutine code.
    i64 run_task(core::task<i64> operation);
} // namespace astia::proc
