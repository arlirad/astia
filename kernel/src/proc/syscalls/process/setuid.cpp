#include <syscall.hpp>

namespace astia::proc::syscalls {


    /// ABI ID: setuid.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's negative errno.
    struct setuid : sys::syscall_handler<setuid, sys::syscall_id::setuid,
                                                 sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor setuid_descriptor = setuid::descriptor;


} // namespace astia::proc::syscalls
