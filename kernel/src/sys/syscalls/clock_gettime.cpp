#include <syscall.hpp>

namespace astia::sys::syscalls {


    /// ABI ID: clock_gettime.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: sys subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's negative errno.
    struct clock_gettime
        : sys::syscall_handler<clock_gettime, sys::syscall_id::clock_gettime,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor clock_gettime_descriptor =
        clock_gettime::descriptor;


} // namespace astia::sys::syscalls
