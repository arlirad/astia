#include <astia/api/aio.h>

#include "../../sys/aio.hpp"

#include <syscall.hpp>
namespace astia::fs::syscall {
    i64 io_destroy(u64 ring) {
        return sys::io_destroy(reinterpret_cast<astia_rings*>(static_cast<usize>(ring)));
    }
}

namespace astia::fs::syscalls {

    /// io_destroy - Destroy an asynchronous I/O ring.
    ///
    /// Synopsis:
    ///     long io_destroy(astia_rings *ring);
    ///
    /// Description:
    ///     Releases a ring owned by the calling process and removes its
    ///     userspace mapping. Completed parked operations are reclaimed before
    ///     the ring is checked for work still in flight.
    ///
    /// Return Value:
    ///     Returns zero after the ring and its mapping are released. It does
    ///     not wait for operations that are still parked.
    ///
    /// Errors:
    ///     -EINVAL: ring is null, is not a live ring, or is not the caller's ring.
    ///     -EBUSY: At least one operation is still parked and could later post a
    ///     completion.
    ///
    /// Notes:
    ///     The caller must stop submitting SQEs before destruction. CQEs already
    ///     reaped by userspace have no further kernel-side ownership; parked
    ///     operations retain their pinned buffers and descriptor references
    ///     until they complete, so they prevent destruction.
    struct io_destroy : sys::syscall_handler<io_destroy, sys::syscall_id::io_destroy,
                                             sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::fs::syscall::io_destroy>(context);
        }
    };

    extern const sys::syscall_descriptor io_destroy_descriptor = io_destroy::descriptor;

} // namespace astia::fs::syscalls
