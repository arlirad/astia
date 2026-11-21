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

    /// thread_create2 - Create a user thread with two arguments.
    ///
    /// Synopsis:
    ///     long thread_create2(void (*entry)(void *, void *), void *stack_top, void
    ///     *argument0, void *argument1);
    ///
    /// Description:
    ///     Creates a thread and passes both arguments directly to entry on the
    ///     supplied aligned user stack.
    ///
    /// Return Value:
    ///     Returns the new thread ID or a negative errno.
    ///
    /// Errors:
    ///     -EAGAIN: The thread limit or memory limit was reached.
    ///     -EFAULT: The entry point, stack, or argument pointer is invalid.
    ///     -ESRCH: There is no current process.
    struct thread_create2
        : sys::syscall_handler<thread_create2, sys::syscall_id::thread_create2,
                               sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            u64   entry      = static_cast<u64>(context.arguments[0]);
            u64   stack_top  = static_cast<u64>(context.arguments[1]);
            u64   argument0  = static_cast<u64>(context.arguments[2]);
            u64   argument1  = static_cast<u64>(context.arguments[3]);
            u64   stack_base = static_cast<u64>(context.arguments[4]);
            u64   stack_size = static_cast<u64>(context.arguments[5]);
            auto* owner      = current_process();
            if (owner == nullptr || !detail::valid_thread_start(entry, stack_top) ||
                !sys::user_ptr<u8>(argument0).validate() ||
                (argument1 != 0 && !sys::user_ptr<u8>(argument1).validate()) ||
                ((stack_base != 0 || stack_size != 0) &&
                 !detail::valid_thread_stack_metadata(stack_base, stack_size,
                                                      stack_top))) {
                return -EFAULT;
            }
            auto value =
                owner->create_thread(entry, stack_top, argument0, argument1, false,
                                     stack_base, static_cast<usize>(stack_size));
            return value ? static_cast<i64>(value->tid()) : -EAGAIN;
        }
    };

    extern const sys::syscall_descriptor thread_create2_descriptor =
        thread_create2::descriptor;

} // namespace astia::proc::syscalls
