#include <astia/api/errno.h>

#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// getppid - Return the parent process identifier.
    ///
    /// Synopsis:
    ///     long getppid(void);
    ///
    /// Return Value:
    ///     Returns the parent process identifier or a negative errno.
    struct getppid : sys::syscall_handler<getppid, sys::syscall_id::getppid,
                                          sys::syscall_mode::sync> {
        static i64 invoke() {
            auto* process = current_process();
            return process == nullptr ? -ESRCH
                                      : static_cast<i64>(raw_id(process->parent_pid()));
        }
    };

    extern const sys::syscall_descriptor getppid_descriptor = getppid::descriptor;

} // namespace astia::proc::syscalls
