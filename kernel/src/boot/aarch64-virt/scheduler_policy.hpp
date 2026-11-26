#pragma once

#include <astia/common.hpp>

// Bounded scheduler policy for the AArch64 boot-CPU proof.  It is pure data
// so the host tests can exercise runnable selection, deadline wake, and
// duplicate-wake rejection without an AArch64 context switch.
namespace astia::boot::aarch64::virt::sched::policy {
    inline constexpr u32 max_threads = 4;

    enum class thread_state : u8 {
        free,
        runnable,
        running,
        sleeping,
        finished,
    };

    struct thread {
        thread_state state            = thread_state::free;
        u64          wake_deadline_ms = 0;
        u32          wake_count       = 0;
        bool         started          = false;
    };

    [[nodiscard]] constexpr bool deadline_after(u64 now_ms, u32 delay_ms, u64& due_ms) {
        if (delay_ms == 0 || now_ms > ~u64{0} - delay_ms)
            return false;

        due_ms = now_ms + delay_ms;

        return true;
    }

    [[nodiscard]] constexpr bool sleep_until(thread& value, u64 deadline_ms) {
        if (value.state != thread_state::running || deadline_ms == 0)
            return false;

        value.wake_deadline_ms = deadline_ms;
        value.state            = thread_state::sleeping;

        return true;
    }

    [[nodiscard]] constexpr bool wake(thread& value) {
        if (value.state != thread_state::sleeping || value.wake_deadline_ms == 0)
            return false;

        value.state            = thread_state::runnable;
        value.wake_deadline_ms = 0;

        ++value.wake_count;

        return true;
    }

    [[nodiscard]] constexpr bool wake_if_due(thread& value, u64 now_ms) {
        if (value.state != thread_state::sleeping || value.wake_deadline_ms == 0 ||
            value.wake_deadline_ms > now_ms)
            return false;

        return wake(value);
    }

    [[nodiscard]] constexpr i32 next_runnable(const thread* table, u32 count,
                                              i32 current) {
        if (table == nullptr || count == 0 || count > max_threads)
            return -1;

        for (u32 step = 1; step <= count; ++step) {
            const u32 index = (static_cast<u32>(current) + step) % count;

            if (table[index].state == thread_state::runnable ||
                table[index].state == thread_state::running)
                return static_cast<i32>(index);
        }

        return -1;
    }

    [[nodiscard]] constexpr bool finished(const thread& value) {
        return value.state == thread_state::finished;
    }

    [[nodiscard]] constexpr bool sleeping(const thread& value) {
        return value.state == thread_state::sleeping;
    }
} // namespace astia::boot::aarch64::virt::sched::policy
