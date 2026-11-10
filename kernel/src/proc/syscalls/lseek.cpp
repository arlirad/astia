#include "lseek.hpp"

#include "../proc.hpp"
#include <astia/api/errno.h>

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
