#pragma once

#include <astia/common.hpp>
#include <astia/signal.hpp>

namespace astia::ipc {

    using futex_operation_mask                              = u32;
    inline constexpr futex_operation_mask futex_private_bit = 1u << 7;
    inline constexpr futex_operation_mask futex_cancelable_bit = 1u << 8;

    enum class futex_operation : u32 {
        wait = 0,
        wake = 1,
    };

} // namespace astia::ipc

namespace astia::proc {

    class process;
    class thread;

    i64 futex_wait(u64 address, u32 expected, u64 timeout, bool cancellation_point,
                   bool shared);
    i64 futex_wake(u64 address, u32 count, bool shared);

    bool futex_interrupt_process(process* owner, signal_number signal);
    bool futex_interrupt_thread(thread* target);

    /// Remove all futex waiters owned by a process before its thread table is
    /// destroyed.  Waiter slots retain their thread ownership independently,
    /// so a wake detached on another CPU cannot use a freed raw pointer.
    void futex_remove_process(process* owner);
    void futex_tick();

} // namespace astia::proc
