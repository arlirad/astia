#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getcwd
        : sys::syscall_handler<getcwd, sys::syscall_id::getcwd, sys::syscall_mode::sync> {
        static i64 invoke(sys::user_ptr<char> buffer, u64 length) {
            auto* owner = current_process();
            if (owner == nullptr)
                return -ESRCH;

            if (length == 0)
                return -ERANGE;

            char      copy[fs::path_max]{};
            const i64 rc = owner->copy_cwd(copy, length);
            if (rc < 0)
                return rc;

            const auto destination = buffer.validate(static_cast<usize>(rc));

            return destination &&
                           sys::copy_out(destination.value.bytes(static_cast<usize>(rc)),
                                         copy)
                       ? rc
                       : -EFAULT;
        }
    };
} // namespace astia::proc::syscalls
