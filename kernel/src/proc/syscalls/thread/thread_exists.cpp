#include <astia/api/errno.h>

#include "../../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {

    i64 thread_exists(u32 tid) {
        auto* owner = current_process();
        if (owner == nullptr)
            return -ESRCH;
        auto target = owner->thread_by_tid(tid);
        return !target || target->state() == thread_state::dead ? -ESRCH : 0;
    }

}

namespace astia::proc::syscalls {
    struct thread_exists
        : sys::syscall_handler<thread_exists, sys::syscall_id::thread_exists,
                               sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::thread_exists>(context);
        }
    };

    extern const sys::syscall_descriptor thread_exists_descriptor =
        thread_exists::descriptor;

} // namespace astia::proc::syscalls
