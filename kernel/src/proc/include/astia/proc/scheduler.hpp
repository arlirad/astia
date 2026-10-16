#pragma once

#include <astia/proc/process.hpp>

namespace astia::proc::scheduler {

    /// Phase-1 scheduler seam.  These calls currently maintain the
    /// bootstrap state but do not switch stacks; the implementation
    /// will gain per-CPU run queues with the architecture context port.
    auto init() -> bool;
    auto create_kernel(kernel_entry entry, void* argument,
                       scheduling_class sched_class = scheduling_class::normal,
                       i32 nice = 0, u8 rt_priority = 0) -> mm::shptr<thread>;
    auto spawn_kernel(kernel_entry entry, void* argument,
                      scheduling_class sched_class = scheduling_class::normal,
                      i32 nice = 0, u8 rt_priority = 0) -> bool;
    auto wake(thread* value) -> bool;
    auto tick() -> void;
    auto yield() -> void;
    auto ready() -> bool;

} // namespace astia::proc::scheduler
