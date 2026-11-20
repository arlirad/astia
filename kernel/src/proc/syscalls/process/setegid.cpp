#include <syscall.hpp>

namespace astia::proc::syscalls {


    /// ABI ID: setegid.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's negative errno.
    struct setegid
        : sys::syscall_handler<setegid, sys::syscall_id::setegid,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor setegid_descriptor = setegid::descriptor;


} // namespace astia::proc::syscalls
