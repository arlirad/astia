#include <astia/api/errno.h>

#include "../../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct thread_exists
        : sys::syscall_handler<thread_exists, sys::syscall_id::thread_exists,
                               sys::syscall_mode::sync> {
        static i64 invoke(u32 tid) {
            auto* owner = current_process();
            if (owner == nullptr)
                return -ESRCH;
            auto target = owner->thread_by_tid(tid);
            return !target || target->state() == thread_state::dead ? -ESRCH : 0;
        }
    };

    extern const sys::syscall_descriptor thread_exists_descriptor =
        thread_exists::descriptor;

} // namespace astia::proc::syscalls
