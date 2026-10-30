#pragma once

#include "process.hpp"

namespace astia::proc::scheduler {

    enum class thread_options : u8 {
        pinned     = 0,
        migratable = 1u << 0,
    };

    /// Initialize the scheduler state for one logical CPU. Threads are pinned
    /// to this CPU until migration and TLB shootdown are implemented.
    bool init();
    bool init_cpu(u32 cpu_index);
    bool start_cpu(u32 cpu_index);
    mm::shptr<thread>
    create_kernel(kernel_entry entry, void* argument,
                  scheduling_class sched_class = scheduling_class::normal, i32 nice = 0,
                  u8 rt_priority = 0, thread_options options = thread_options::pinned);
    mm::shptr<thread>
         create_kernel_on(u32 cpu_index, kernel_entry entry, void* argument,
                          scheduling_class sched_class = scheduling_class::normal,
                          i32 nice = 0, u8 rt_priority = 0,
                          thread_options options = thread_options::pinned);
    bool spawn_kernel(kernel_entry entry, void* argument,
                      scheduling_class sched_class = scheduling_class::normal,
                      i32 nice = 0, u8 rt_priority = 0,
                      thread_options options = thread_options::pinned);
    /// Move a queued runnable thread to another initialized CPU.
    bool migrate(thread* value, u32 cpu_index);
    bool wake(thread* value);
    void tick();
    void yield();
    bool ready();

} // namespace astia::proc::scheduler
