#include "setsid.hpp"

#include "../proc.hpp"

#include <syscall.hpp>
namespace astia::proc::syscall {
    i64 setsid() {
        return proc::setsid();
    }
}

namespace astia::proc::syscalls {

    /// setsid - create a new session for the caller.
    ///
    /// Synopsis:
    ///     long setsid(void);
    ///
    /// Errors:
    ///     -EPERM: The caller is already a process-group leader.
    ///     -ESRCH: There is no current process.

    /// ABI ID: setsid.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct setsid : sys::syscall_handler<setsid, sys::syscall_id::setsid,
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor setsid_descriptor = setsid::descriptor;

} // namespace astia::proc::syscalls
