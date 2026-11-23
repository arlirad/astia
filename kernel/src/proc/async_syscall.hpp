#pragma once

#include <astia/core/task.hpp>

namespace astia::proc {

    /// Run an async syscall body and park the current thread until it
    /// completes.  The dispatcher calls this for async descriptors.
    i64 run_async_syscall(core::task<i64> operation);

} // namespace astia::proc
