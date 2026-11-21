#include "getpid.hpp"

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

    /// ABI ID: getpid.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct getpid : sys::syscall_handler<getpid, sys::syscall_id::getpid,
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getpid_descriptor = getpid::descriptor;

} // namespace astia::proc::syscalls
