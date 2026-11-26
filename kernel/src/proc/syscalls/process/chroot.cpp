#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct chroot
        : sys::syscall_handler<chroot, sys::syscall_id::chroot, sys::syscall_mode::sync> {
        static i64 invoke(sys::user_ptr<char> path) {
            auto* owner = current_process();
            if (owner == nullptr)
                return -ESRCH;

            char copy[fs::path_max]{};

            if (!sys::copy_in_string(span<char>(copy, sizeof copy), path))
                return -EFAULT;

            return owner->chroot(copy);
        }
    };
} // namespace astia::proc::syscalls
