#include <astia/api/errno.h>

#include "../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct getpid
        : sys::syscall_handler<getpid, sys::syscall_id::getpid, sys::syscall_mode::sync> {
        static i64 invoke() {
            auto* process = current_process();

            return process == nullptr ? -ESRCH : static_cast<i64>(raw_id(process->pid()));
        }
    };
} // namespace astia::proc::syscalls
