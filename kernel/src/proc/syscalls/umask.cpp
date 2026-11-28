#include <astia/api/errno.h>

#include "proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct umask
        : sys::syscall_handler<umask, sys::syscall_id::umask, sys::syscall_mode::sync> {
        static i64 invoke(u32 mode) {
            auto* owner = current_process();
            if (owner == nullptr)
                return -ESRCH;

            return owner->set_umask(mode);
        }
    };
} // namespace astia::proc::syscalls
