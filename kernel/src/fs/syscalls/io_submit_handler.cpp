#include <syscall.hpp>

namespace astia::fs::syscalls {

    /// io_submit - Submit published asynchronous I/O operations.
    ///
    /// Synopsis:
    ///     long io_submit(astia_ring *ring, uint32_t count);
    ///
    /// Return Value:
    ///     Returns zero or a negative errno.
    struct io_submit_syscall
        : sys::syscall_handler<io_submit_syscall, sys::syscall_id::io_submit,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor io_submit_descriptor =
        io_submit_syscall::descriptor;

} // namespace astia::fs::syscalls
