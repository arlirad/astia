#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// alarm - Schedule delivery of a process alarm.
    ///
    /// Synopsis:
    ///     long alarm(long seconds);
    ///
    /// Return Value:
    ///     Returns the previous alarm value or a negative errno.
    struct alarm_syscall : sys::syscall_handler<alarm_syscall, sys::syscall_id::alarm,
                                                sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor alarm_descriptor = alarm_syscall::descriptor;

} // namespace astia::proc::syscalls
