#include "getpgrp.hpp"

#include "../proc.hpp"

#include <syscall.hpp>
namespace astia::proc::syscall {
    i64 getpgrp() {
        return static_cast<i64>(raw_id(proc::getpgrp()));
    }
}

namespace astia::proc::syscalls {

    /// getpgrp - return the calling process group ID.
    ///
    /// Synopsis:
    ///     long getpgrp(void);

    /// ABI ID: getpgrp.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct getpgrp : sys::syscall_handler<getpgrp, sys::syscall_id::getpgrp,
                                          sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor getpgrp_descriptor = getpgrp::descriptor;

} // namespace astia::proc::syscalls
