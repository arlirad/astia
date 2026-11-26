#pragma once

#include <astia/common.hpp>

#include "scheduler_policy.hpp"

// Bounded, statically allocated cooperative scheduler for the AArch64 virt
// boot CPU.  Two kernel contexts are enough for the first timer-to-scheduler
// handoff, so every context and stack is a fixed array in .bss: the stage has
// no heap and adding one is out of scope.  The deadline queue owns a sleep
// request until it fires; the scheduler only records the wake.
namespace astia::boot::aarch64::virt::sched {
    using entry = void (*)(void*);

    inline constexpr u32 stack_bytes = 16 * 1024;

    struct evidence {
        u32 created        = 0;
        u32 started        = 0;
        u32 sleeps         = 0;
        u32 wakes          = 0;
        u32 deadline_wakes = 0;
        u32 spurious_wakes = 0;
        u32 retires        = 0;
    };

    bool                 init();
    i32                  create(entry function, void* argument);
    bool                 start();
    bool                 sleep_until(u64 deadline_ms);
    void                 yield();
    void                 reschedule_from_irq();
    void                 timer_tick();
    const evidence&      stats();
    policy::thread_state thread_state(i32 id);
    u32                  wake_count(i32 id);
    bool                 finished(i32 id);
    bool                 sleeping(i32 id);
} // namespace astia::boot::aarch64::virt::sched
