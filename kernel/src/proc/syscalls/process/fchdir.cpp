#include <astia/api/errno.h>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../process.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct fchdir
        : sys::syscall_handler<fchdir, sys::syscall_id::fchdir, sys::syscall_mode::sync> {
        static i64 invoke(i64 raw_fd) {
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
    };

    extern const sys::syscall_descriptor fchdir_descriptor = fchdir::descriptor;

} // namespace astia::proc::syscalls
