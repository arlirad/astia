#include <syscall.hpp>

namespace astia::net::syscalls {


    /// ABI ID: 78.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: net subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's negative errno.
    struct sendto
        : sys::syscall_handler<sendto, static_cast<sys::syscall_id>(78),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor sendto_descriptor = sendto::descriptor;


} // namespace astia::net::syscalls
