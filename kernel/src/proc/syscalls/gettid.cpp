#include "gettid.hpp"

#include <astia/api/errno.h>

#include "../proc.hpp"

#include <syscall.hpp>
namespace astia::proc::syscall {
    i64 gettid() {
        auto* thread = current_thread();
        return thread == nullptr ? -ESRCH : static_cast<i64>(thread->tid());
    }
}

namespace astia::proc::syscalls {

    /// gettid - return the calling thread ID.
    ///
    /// Synopsis:
    ///     long gettid(void);

    /// ABI ID: gettid.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct gettid : sys::syscall_handler<gettid, sys::syscall_id::gettid,
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor gettid_descriptor = gettid::descriptor;

} // namespace astia::proc::syscalls
