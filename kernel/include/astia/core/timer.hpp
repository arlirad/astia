#pragma once

#include <astia/common.hpp>

namespace astia::core {
    /// Scheduler tick hook: wakes every coroutine whose delay expired.
    /// Runs in the timer interrupt, so it only queues resumptions.
    void run_timers(u64 now);

    namespace detail {
        /// Arms a one-shot callback for `ticks` from now.  Returns false when
        /// there is no timer source or the queue is full.
        bool arm_timer_callback(void* owner, void (*callback)(void*), u64 ticks);

        /// Removes `owner` from the queue; false when it already fired.
        bool disarm_timer(void* owner);
    } // namespace detail
} // namespace astia::core
