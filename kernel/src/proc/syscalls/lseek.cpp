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
    struct lseek
        : sys::syscall_handler<lseek, sys::syscall_id::lseek, sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::lseek>(context);
        }
    };

    extern const sys::syscall_descriptor lseek_descriptor = lseek::descriptor;

} // namespace astia::proc::syscalls
