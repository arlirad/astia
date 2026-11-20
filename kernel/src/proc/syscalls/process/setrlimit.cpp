#include <syscall.hpp>

namespace astia::proc::syscalls {


    /// ABI ID: setrlimit.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's negative errno.
    struct setrlimit
        : sys::syscall_handler<setrlimit, sys::syscall_id::setrlimit,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor setrlimit_descriptor =
        setrlimit::descriptor;


} // namespace astia::proc::syscalls
