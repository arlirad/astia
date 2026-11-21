#include <astia/api/errno.h>

#include "../../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {

    i64 thread_cancel_pending() {
        auto* self = current_thread();
        return self == nullptr ? -ESRCH : (self->cancellation_pending() ? 1 : 0);
    }

}

namespace astia::proc::syscalls {
    struct thread_cancel_pending
        : sys::syscall_handler<thread_cancel_pending,
                               sys::syscall_id::thread_cancel_pending,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::thread_cancel_pending>(
                context);
        }
    };

    extern const sys::syscall_descriptor thread_cancel_pending_descriptor =
        thread_cancel_pending::descriptor;

} // namespace astia::proc::syscalls
