#include <astia/api/errno.h>

#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getppid : sys::syscall_handler<getppid, sys::syscall_id::getppid,
                                          sys::syscall_mode::sync> {
        static i64 invoke() {
            auto* process = current_process();

            return process == nullptr ? -ESRCH
                                      : static_cast<i64>(raw_id(process->parent_pid()));
        }
    };
} // namespace astia::proc::syscalls
