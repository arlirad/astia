#include <astia/api/errno.h>

#include "../../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct thread_cancel_pending
        : sys::syscall_handler<thread_cancel_pending,
                               sys::syscall_id::thread_cancel_pending,
                               sys::syscall_mode::sync> {
        static i64 invoke() {
            auto* self = current_thread();
            return self == nullptr ? -ESRCH : (self->cancellation_pending() ? 1 : 0);
        }
    };

    extern const sys::syscall_descriptor thread_cancel_pending_descriptor =
        thread_cancel_pending::descriptor;

} // namespace astia::proc::syscalls
