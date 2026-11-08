#pragma once

#include <astia/common.hpp>

namespace astia::ipc {

    using futex_operation_mask                              = u32;
    inline constexpr futex_operation_mask futex_private_bit = 1u << 7;

    enum class futex_operation : u32 {
        wait = 0,
        wake = 1,
    };

} // namespace astia::ipc

namespace astia::proc {

    class process;

    /// Remove all futex waiters owned by a process before its thread table is
    /// destroyed.  Waiter slots retain their thread ownership independently,
    /// so a wake detached on another CPU cannot use a freed raw pointer.
    void futex_remove_process(process* owner);
    void futex_tick();

} // namespace astia::proc
