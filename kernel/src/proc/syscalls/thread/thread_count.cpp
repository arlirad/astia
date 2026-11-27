#include <astia/api/errno.h>

#include "../../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct thread_count
        : sys::syscall_handler<thread_count, sys::syscall_id::thread_count,
                               sys::syscall_mode::sync> {
        static i64 invoke() {
            auto* owner = current_process();
            if (owner == nullptr)
                return -ESRCH;

            return static_cast<i64>(owner->live_thread_count());
        }
    };
} // namespace astia::proc::syscalls
