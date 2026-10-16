#pragma once

#include "process.hpp"

namespace astia::proc::scheduler {

    /// Phase-1 scheduler seam.  These calls currently maintain the
    /// bootstrap state but do not switch stacks; the implementation
    /// will gain per-CPU run queues with the architecture context port.
    bool init();
    mm::shptr<thread>
    create_kernel(kernel_entry entry, void* argument,
                  scheduling_class sched_class = scheduling_class::normal, i32 nice = 0,
                  u8 rt_priority = 0);
    bool spawn_kernel(kernel_entry entry, void* argument,
                      scheduling_class sched_class = scheduling_class::normal,
                      i32 nice = 0, u8 rt_priority = 0);
    bool wake(thread* value);
    void tick();
    void yield();
    bool ready();

} // namespace astia::proc::scheduler
