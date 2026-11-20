#include <syscall.hpp>

namespace astia::proc::syscalls {


    /// ABI ID: process_snapshot.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's negative errno.
    struct process_snapshot
        : sys::syscall_handler<process_snapshot,
                               sys::syscall_id::process_snapshot,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor process_snapshot_descriptor =
        process_snapshot::descriptor;


} // namespace astia::proc::syscalls
