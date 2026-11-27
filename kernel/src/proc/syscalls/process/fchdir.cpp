#include <astia/api/errno.h>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../process.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct fchdir : sys::syscall_handler<fchdir, sys::syscall_id::fchdir,
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke(i64 raw_fd) {
            auto* owner = current_process();
            if (owner == nullptr)
                co_return -ESRCH;

            auto mapped = mapper<fd_t>::from(raw_fd);

            if (!mapped.has_value)
                co_return -EBADF;

            auto file = owner->descriptor(mapped.value);
            if (!file)
                co_return -EBADF;

            if (!file->mode().is_directory())
                co_return -ENOTDIR;

            if (file->path() != nullptr && !owner->path_within_root(file->path()))
                co_return -ENOENT;

            co_return owner->change_directory(*file);
        }
    };
} // namespace astia::proc::syscalls
