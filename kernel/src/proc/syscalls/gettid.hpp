#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>
namespace astia::proc::syscall {
    /// gettid - return the calling thread ID.
    ///
    /// Synopsis:
    ///     long gettid(void);
    i64 gettid();
}

namespace astia::proc::syscall_handlers {

    struct gettid_syscall : sys::syscall_handler<gettid_syscall, sys::syscall_id::gettid,
                                                 sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::proc::syscall_handlers
