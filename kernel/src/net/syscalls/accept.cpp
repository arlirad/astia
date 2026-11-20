#include <syscall.hpp>

namespace astia::net::syscalls {


    /// ABI ID: 70.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: net subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's negative errno.
    struct accept
        : sys::syscall_handler<accept, static_cast<sys::syscall_id>(70),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor accept_descriptor = accept::descriptor;


} // namespace astia::net::syscalls
