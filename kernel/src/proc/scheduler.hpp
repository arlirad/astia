#pragma once

#include "process.hpp"

namespace astia::proc::scheduler {

    /// Initialize the scheduler state for one logical CPU. Threads are pinned
    /// to this CPU until migration and TLB shootdown are implemented.
    bool init();
    bool init_cpu(u32 cpu_index);
    bool start_cpu(u32 cpu_index);
    mm::shptr<thread>
    create_kernel(kernel_entry entry, void* argument,
                  scheduling_class sched_class = scheduling_class::normal, i32 nice = 0,
                  u8 rt_priority = 0);
    mm::shptr<thread>
    create_kernel_on(u32 cpu_index, kernel_entry entry, void* argument,
                     scheduling_class sched_class = scheduling_class::normal, i32 nice = 0,
                     u8 rt_priority = 0);
    bool spawn_kernel(kernel_entry entry, void* argument,
                      scheduling_class sched_class = scheduling_class::normal,
                      i32 nice = 0, u8 rt_priority = 0);
    /// Move a queued runnable thread to another initialized CPU.
    bool migrate(thread* value, u32 cpu_index);
    bool wake(thread* value);
    void tick();
    void yield();
    bool ready();

} // namespace astia::proc::scheduler
