#include <syscall.hpp>

namespace astia::proc::syscalls {

    struct process_snapshot_syscall
        : sys::syscall_handler<process_snapshot_syscall,
                               sys::syscall_id::process_snapshot,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor process_snapshot_descriptor =
        process_snapshot_syscall::descriptor;

} // namespace astia::proc::syscalls
