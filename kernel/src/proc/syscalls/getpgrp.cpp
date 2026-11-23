#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// getpgrp - return the calling process group ID.
    ///
    /// Synopsis:
    ///     long getpgrp(void);
    struct getpgrp : sys::syscall_handler<getpgrp, sys::syscall_id::getpgrp,
                                          sys::syscall_mode::sync> {
        static i64 invoke() {
            return static_cast<i64>(raw_id(proc::getpgrp()));
        }
    };

} // namespace astia::proc::syscalls
