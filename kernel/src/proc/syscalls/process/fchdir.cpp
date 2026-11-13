#include "fchdir.hpp"

#include "../../proc.hpp"
#include "../../process.hpp"
#include "../../mapping.hpp"
#include <astia/api/errno.h>

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
