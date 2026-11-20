#include <syscall.hpp>

namespace astia::proc::syscalls {


    /// ABI ID: shm_open.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's negative errno.
    struct shm_open
        : sys::syscall_handler<shm_open, sys::syscall_id::shm_open,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor shm_open_descriptor =
        shm_open::descriptor;


} // namespace astia::proc::syscalls
