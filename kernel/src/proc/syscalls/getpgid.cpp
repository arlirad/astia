#include "getpgid.hpp"

#include <astia/api/errno.h>

#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {
    i64 getpgid(i64 raw_target) {
        if (raw_target < 0 || raw_target > 0xffffffffll)
            return -ESRCH;
        return proc::getpgid(pid_from_raw(static_cast<u32>(raw_target)));
    }
}

namespace astia::proc::syscalls {

    /// getpgid - Return a process group identifier.
    ///
    /// Synopsis:
    ///     long getpgid(int pid);
    ///
    /// Return Value:
    ///     Returns the process group identifier or a negative errno.
    /// ABI ID: getpgid.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct getpgid : sys::syscall_handler<getpgid, sys::syscall_id::getpgid,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getpgid_descriptor = getpgid::descriptor;

} // namespace astia::proc::syscalls
