#include <astia/api/errno.h>
#include <astia/api/syscalls.h>

#include "../dl.hpp"
#include "proc/proc.hpp"

#include <syscall.hpp>

namespace astia::dl::syscalls {
    struct dlunmap : sys::syscall_handler<dlunmap, sys::syscall_id::dlunmap,
                                          sys::syscall_mode::sync> {
        static i64 invoke(u64 base) {
            auto* process = proc::current_process();
            if (process == nullptr)
                return -ESRCH;

            return astia::dl::unmap(*process, base);
        }
    };
} // namespace astia::dl::syscalls
