#include "thread_exists.hpp"

#include <astia/api/errno.h>

#include "../../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {

    i64 thread_exists(u32 tid) {
        auto* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        auto target = owner->thread_by_tid(tid);
        return !target || target->state() == thread_state::dead ? -ESRCH : 0;
    }

}

namespace astia::proc::syscalls {

    /// ABI ID: thread_exists.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct thread_exists
        : sys::syscall_handler<thread_exists, sys::syscall_id::thread_exists,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor thread_exists_descriptor =
        thread_exists::descriptor;

} // namespace astia::proc::syscalls
