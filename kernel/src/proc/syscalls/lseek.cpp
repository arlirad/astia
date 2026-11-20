#include "lseek.hpp"

#include <astia/api/errno.h>

#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {
    i64 lseek(i64 raw_fd, i64 offset, i64 raw_whence) {
        auto* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        if (raw_fd < 0 || raw_fd >= static_cast<i64>(max_process_fds))
            return -EBADF;
        if (raw_whence < 0 || raw_whence > 2)
            return -EINVAL;
        auto object = owner->descriptor(fd_t{static_cast<i32>(raw_fd)});
        if (!object)
            return -EBADF;
        return object->seek(offset, static_cast<i32>(raw_whence));
    }
}

namespace astia::proc::syscalls {

    /// ABI ID: lseek.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct lseek
        : sys::syscall_handler<lseek, sys::syscall_id::lseek, sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor lseek_descriptor = lseek::descriptor;

} // namespace astia::proc::syscalls
