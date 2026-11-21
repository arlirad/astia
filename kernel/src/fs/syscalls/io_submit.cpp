#include <astia/api/aio.h>

#include "../../sys/aio.hpp"

#include <syscall.hpp>

namespace astia::fs::syscall {
    i64 io_submit(u64 ring, u32 submit) {
        return sys::io_submit(reinterpret_cast<astia_rings*>(static_cast<usize>(ring)),
                              submit);
    }
}

namespace astia::fs::syscalls {

    /// io_submit - Submit published asynchronous I/O operations.
    ///
    /// Synopsis:
    ///     long io_submit(astia_ring *ring, uint32_t count);
    ///
    /// Return Value:
    ///     Returns zero or a negative errno.
    struct io_submit : sys::syscall_handler<io_submit, sys::syscall_id::io_submit,
                                            sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::fs::syscall::io_submit>(context);
        }
    };

    extern const sys::syscall_descriptor io_submit_descriptor = io_submit::descriptor;

} // namespace astia::fs::syscalls
