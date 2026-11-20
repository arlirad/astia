#include "io_submit.hpp"

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
    /// ABI ID: io_submit.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: fs subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct io_submit : sys::syscall_handler<io_submit, sys::syscall_id::io_submit,
                                            sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor io_submit_descriptor = io_submit::descriptor;

} // namespace astia::fs::syscalls
