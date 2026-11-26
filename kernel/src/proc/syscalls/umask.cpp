#include <astia/api/errno.h>

#include "proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct umask
        : sys::syscall_handler<umask, sys::syscall_id::umask, sys::syscall_mode::sync> {
        static i64 invoke(int mode) {
            auto* owner = current_process();

            if (owner == nullptr || mode < 0)
                return -ESRCH;

            return owner->set_umask(static_cast<u32>(mode));
        }
    };
} // namespace astia::proc::syscalls
