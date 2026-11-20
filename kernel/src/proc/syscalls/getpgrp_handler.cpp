#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// getpgrp - return the calling process group ID.
    ///
    /// Synopsis:
    ///     long getpgrp(void);
    struct getpgrp_syscall
        : sys::syscall_handler<getpgrp_syscall, sys::syscall_id::getpgrp,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getpgrp_descriptor = getpgrp_syscall::descriptor;

} // namespace astia::proc::syscalls
