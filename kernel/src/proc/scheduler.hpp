#pragma once

#include <astia/arch/platform.hpp>
#include <astia/resource_limits.hpp>

#include "process.hpp"

namespace astia::proc::scheduler {
    inline constexpr u32 max_cpu_count      = arch::platform::max_cpu_count;
    inline constexpr u32 run_queue_capacity = resource_limits::queue::scheduler_runnable;
    inline constexpr u32 run_queue_rebalance_slack = 1;
    inline constexpr u32 nice_charge_scale         = 1024;

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
    bool runnable_ready();
    void yield();
    bool ready();
} // namespace astia::proc::scheduler
