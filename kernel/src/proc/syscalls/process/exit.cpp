#include <astia/api/errno.h>
#include <astia/log.hpp>

#include "../../proc.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {

    /// exit - terminate the calling thread.
    ///
    /// Synopsis:
    ///     long exit(long status);
    ///
    /// Return Value:
    ///     Does not return to the caller.
    struct exit
        : sys::syscall_handler<exit, sys::syscall_id::exit, sys::syscall_mode::sync> {
        static i64 invoke(i64 status) {
            auto* thread = current_thread();
            if (thread == nullptr)
                return -ESRCH;
            astia::log::context<"proc">::info(
                "user thread exit pid=%d tid=%d status=%ld",
                thread->owner() ? static_cast<int>(raw_id(thread->owner()->pid())) : -1,
                static_cast<int>(thread->tid()), status);
            if (thread->owner() != nullptr)
                thread->owner()->request_exit(thread, static_cast<i32>(status));
            thread->exit(reinterpret_cast<void*>(static_cast<usize>(status)));
            __builtin_unreachable();
        }
    };

    extern const sys::syscall_descriptor exit_descriptor = exit::descriptor;

} // namespace astia::proc::syscalls
