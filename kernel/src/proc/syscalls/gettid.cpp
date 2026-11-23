#include <astia/api/errno.h>

#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// gettid - return the calling thread ID.
    ///
    /// Synopsis:
    ///     long gettid(void);
    struct gettid
        : sys::syscall_handler<gettid, sys::syscall_id::gettid, sys::syscall_mode::sync> {
        static i64 invoke() {
            auto* thread = current_thread();
            return thread == nullptr ? -ESRCH : static_cast<i64>(thread->tid());
        }
    };

} // namespace astia::proc::syscalls
