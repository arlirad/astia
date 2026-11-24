#include <astia/api/errno.h>

#include "../../proc/proc.hpp"
#include "../../sys/user_mem.hpp"
#include "../filesystem.hpp"

#include <syscall.hpp>

namespace astia::fs::syscalls {

    struct unmount : sys::syscall_handler<unmount, sys::syscall_id::unmount,
                                          sys::syscall_mode::sync> {
        static i64 invoke(sys::user_ptr<char> target) {
            auto* current = proc::current_process();
            if (current == nullptr)
                return -ESRCH;
            if (current->euid() != 0)
                return -EPERM;
            char path[path_max]{};
            if (!sys::copy_in_string(span<char>(path, sizeof path), target))
                return -EFAULT;
            return unmount_type(path);
        }
    };

} // namespace astia::fs::syscalls
