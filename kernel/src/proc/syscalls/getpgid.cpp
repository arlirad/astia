#include <astia/api/errno.h>

#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// getpgid - Return a process group identifier.
    ///
    /// Synopsis:
    ///     long getpgid(int pid);
    ///
    /// Return Value:
    ///     Returns the process group identifier or a negative errno.
    struct getpgid : sys::syscall_handler<getpgid, sys::syscall_id::getpgid,
                                          sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            i64 raw_target = static_cast<i64>(context.arguments[0]);
            if (raw_target < 0 || raw_target > 0xffffffffll)
                return -ESRCH;
            return proc::getpgid(pid_from_raw(static_cast<u32>(raw_target)));
        }
    };

    extern const sys::syscall_descriptor getpgid_descriptor = getpgid::descriptor;

} // namespace astia::proc::syscalls
