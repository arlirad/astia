#include "getppid.hpp"

#include <astia/api/errno.h>

#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {
    i64 getppid() {
        auto* process = current_process();
        return process == nullptr ? -ESRCH
                                  : static_cast<i64>(raw_id(process->parent_pid()));
    }
}

namespace astia::proc::syscalls {

    /// getppid - Return the parent process identifier.
    ///
    /// Synopsis:
    ///     long getppid(void);
    ///
    /// Return Value:
    ///     Returns the parent process identifier or a negative errno.

    /// ABI ID: getppid.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct getppid : sys::syscall_handler<getppid, sys::syscall_id::getppid,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getppid_descriptor = getppid::descriptor;

} // namespace astia::proc::syscalls
