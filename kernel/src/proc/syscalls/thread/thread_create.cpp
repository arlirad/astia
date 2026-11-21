#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>

#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "thread_create_common.hpp"
#include "user_mem.hpp"

#include <syscall.hpp>
namespace astia::proc::syscalls {

    /// thread_create - Create a user thread.
    ///
    /// Synopsis:
    ///     long thread_create(void (*entry)(void *), void *stack_top, void *argument);
    ///
    /// Description:
    ///     Creates a thread in the current process and starts entry with
    ///     argument on the supplied aligned user stack.
    ///
    /// Return Value:
    ///     Returns the new thread ID or a negative errno.
    ///
    /// Errors:
    ///     -EAGAIN: The thread limit or memory limit was reached.
    ///     -EFAULT: The entry point or user stack is invalid.
    ///     -ESRCH: There is no current process.
    struct thread_create
        : sys::syscall_handler<thread_create, sys::syscall_id::thread_create,
                               sys::syscall_mode::sync> {
        static i64 invoke(u64 entry, u64 stack_top, u64 argument) {
            auto* owner = current_process();
            if (owner == nullptr || !detail::valid_thread_start(entry, stack_top)) {
                return -EFAULT;
            }
            auto value = owner->create_thread(entry, stack_top, argument);
            return value ? static_cast<i64>(value->tid()) : -EAGAIN;
        }
    };

    extern const sys::syscall_descriptor thread_create_descriptor =
        thread_create::descriptor;

} // namespace astia::proc::syscalls
