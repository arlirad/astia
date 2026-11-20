#include "fchdir.hpp"

#include <astia/api/errno.h>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../process.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {
    i64 fchdir(i64 raw_fd) {
        auto* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        auto mapped = mapper<fd_t>::from(raw_fd);
        if (!mapped.has_value)
            return -EBADF;
        auto file = owner->descriptor(mapped.value);
        if (!file)
            return -EBADF;
        if (!file->path() || !file->mode().is_directory())
            return -ENOTDIR;
        return owner->change_directory(file->path());
    }
}

namespace astia::proc::syscalls {

    /// ABI ID: fchdir.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct fchdir : sys::syscall_handler<fchdir, sys::syscall_id::fchdir,
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor fchdir_descriptor = fchdir::descriptor;

} // namespace astia::proc::syscalls
