#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>

/// alarm - Schedule delivery of a process alarm.
///
/// Synopsis:
///     long alarm(long seconds);
///
/// Return Value:
///     Returns the previous alarm value or a negative errno.
namespace astia::proc::syscall {
    i64 alarm(i64 seconds);
}

namespace astia::proc::syscall_handlers {

    struct alarm_syscall : sys::syscall_handler<alarm_syscall, sys::syscall_id::alarm,
                                                sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

} // namespace astia::proc::syscall_handlers
