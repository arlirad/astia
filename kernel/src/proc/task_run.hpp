#pragma once

#include <astia/core/task.hpp>

namespace astia::proc {
    class thread;

    namespace detail {
        using wait_park_checkpoint = void (*)(thread& waiter);

        /// Test seam: runs after the waiter drains queued resumes and before it
        /// parks.  Null in production; the in-guest regression installs a
        /// checkpoint that yields, injecting the preemption that loses a wakeup.
        extern wait_park_checkpoint wait_park_checkpoint_hook;
    } // namespace detail

    /// Run a task on the current thread and park until it completes.
    /// The async syscall dispatcher and kernel boot/selftest drivers use
    /// this as the blocking boundary for coroutine code.
    i64 run_task(core::task<i64> operation);
} // namespace astia::proc
