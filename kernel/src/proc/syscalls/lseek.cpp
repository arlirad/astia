#include <astia/api/errno.h>

#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct lseek
        : sys::syscall_handler<lseek, sys::syscall_id::lseek, sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            i64   raw_fd     = static_cast<i64>(context.arguments[0]);
            i64   offset     = static_cast<i64>(context.arguments[1]);
            i64   raw_whence = static_cast<i64>(context.arguments[2]);
            auto* owner      = current_process();
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
    };

    extern const sys::syscall_descriptor lseek_descriptor = lseek::descriptor;

} // namespace astia::proc::syscalls
