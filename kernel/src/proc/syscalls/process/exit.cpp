#include "exit.hpp"

#include <astia/api/errno.h>
#include <astia/log.hpp>

#include "../../proc.hpp"

#include <syscall.hpp>
namespace astia::proc::syscall {
    i64 exit(i64 status) {
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
}

namespace astia::proc::syscalls {

    /// exit - terminate the calling thread.
    ///
    /// Synopsis:
    ///     long exit(long status);
    ///
    /// Return Value:
    ///     Does not return to the caller.

    /// ABI ID: exit.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct exit
        : sys::syscall_handler<exit, sys::syscall_id::exit, sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor exit_descriptor = exit::descriptor;

} // namespace astia::proc::syscalls
