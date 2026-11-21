#include <astia/api/errno.h>

#include "../proc.hpp"

#include <syscall.hpp>
namespace astia::proc::syscall {
    i64 getpid() {
        auto* process = current_process();
        return process == nullptr ? -ESRCH : static_cast<i64>(raw_id(process->pid()));
    }
}

namespace astia::proc::syscalls {

    /// getpid - return the calling process ID.
    ///
    /// Synopsis:
    ///     long getpid(void);
    ///
    /// Return Value:
    ///     The calling process ID.
    struct getpid
        : sys::syscall_handler<getpid, sys::syscall_id::getpid, sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::getpid>(context);
        }
    };

    extern const sys::syscall_descriptor getpid_descriptor = getpid::descriptor;

} // namespace astia::proc::syscalls
