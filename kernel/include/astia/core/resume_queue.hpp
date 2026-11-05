#pragma once

#include <astia/core/mpmc_queue.hpp>

#include <coroutine>

namespace astia::core {

    /// Scheduler-owned continuation handoff. Producers may publish a
    /// coroutine from IRQ/task context; only the owning CPU drains it.
    bool schedule_resume(std::coroutine_handle<> continuation);
    void cancel_resume(std::coroutine_handle<> continuation);
    u32  drain_resumes(u32 budget = 256);

} // namespace astia::core
