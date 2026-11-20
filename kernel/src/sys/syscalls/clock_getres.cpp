#include <syscall.hpp>

namespace astia::sys::syscalls {


    /// ABI ID: clock_getres.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: sys subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's negative errno.
    struct clock_getres
        : sys::syscall_handler<clock_getres, sys::syscall_id::clock_getres,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor clock_getres_descriptor =
        clock_getres::descriptor;


} // namespace astia::sys::syscalls
