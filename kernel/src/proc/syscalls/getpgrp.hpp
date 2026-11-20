#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>
namespace astia::proc::syscall {
    /// getpgrp - return the calling process group ID.
    ///
    /// Synopsis:
    ///     long getpgrp(void);
    i64 getpgrp();
}

namespace astia::proc::syscall_handlers {

    struct getpgrp_syscall
        : sys::syscall_handler<getpgrp_syscall, sys::syscall_id::getpgrp,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::proc::syscall_handlers
