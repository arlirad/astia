#include "thread_cancel.hpp"

#include <astia/api/errno.h>

#include "../../../ipc/futex.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {

    i64 thread_cancel(u32 tid) {
        auto* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        auto target = owner->thread_by_tid(tid);
        if (!target || target->state() == thread_state::dead)
            return -ESRCH;
        target->request_cancellation();
        target->wake_join_for_cancellation();
        (void) futex_interrupt_thread(target.get());
        return 0;
    }

}

namespace astia::proc::syscalls {

    /// ABI ID: thread_cancel.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct thread_cancel
        : sys::syscall_handler<thread_cancel, sys::syscall_id::thread_cancel,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor thread_cancel_descriptor =
        thread_cancel::descriptor;

} // namespace astia::proc::syscalls
