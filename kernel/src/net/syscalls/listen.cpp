#include <syscall.hpp>

namespace astia::net::syscalls {


    /// ABI ID: 74.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: net subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's negative errno.
    struct listen
        : sys::syscall_handler<listen, static_cast<sys::syscall_id>(74),
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor listen_descriptor = listen::descriptor;


} // namespace astia::net::syscalls
