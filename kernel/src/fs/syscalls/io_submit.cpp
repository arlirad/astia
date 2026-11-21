#include <astia/api/aio.h>

#include "../../sys/aio.hpp"

#include <syscall.hpp>

namespace astia::fs::syscalls {

    /// io_submit - Submit published asynchronous I/O operations.
    ///
    /// Synopsis:
    ///     long io_submit(astia_ring *ring, uint32_t count);
    ///
    /// Return Value:
    ///     Returns zero or a negative errno.
    struct io_submit : sys::syscall_handler<io_submit, sys::syscall_id::io_submit,
                                            sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            const u64 ring   = static_cast<u64>(context.arguments[0]);
            const u32 submit = static_cast<u32>(context.arguments[1]);
            return sys::io_submit(
                reinterpret_cast<astia_rings*>(static_cast<usize>(ring)), submit);
        }
    };

    extern const sys::syscall_descriptor io_submit_descriptor = io_submit::descriptor;

} // namespace astia::fs::syscalls
