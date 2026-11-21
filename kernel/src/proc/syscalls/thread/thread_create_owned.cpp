#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "thread_create_common.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct thread_create_owned
        : sys::syscall_handler<thread_create_owned, sys::syscall_id::thread_create_owned,
                               sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            const u64 entry      = static_cast<u64>(context.arguments[0]);
            const u64 stack_top  = static_cast<u64>(context.arguments[1]);
            const u64 argument0  = static_cast<u64>(context.arguments[2]);
            const u64 argument1  = static_cast<u64>(context.arguments[3]);
            const u64 stack_base = static_cast<u64>(context.arguments[4]);
            const u64 stack_size = static_cast<u64>(context.arguments[5]);
            auto*     owner      = current_process();
            if (owner == nullptr || !detail::valid_thread_start(entry, stack_top) ||
                !sys::user_ptr<u8>(argument0).validate() ||
                (argument1 != 0 && !sys::user_ptr<u8>(argument1).validate()) ||
                ((stack_base != 0 || stack_size != 0) &&
                 !detail::valid_thread_stack_metadata(stack_base, stack_size, stack_top)))
                return -EFAULT;
            auto value =
                owner->create_thread(entry, stack_top, argument0, argument1, true,
                                     stack_base, static_cast<usize>(stack_size));
            return value ? static_cast<i64>(value->tid()) : -EAGAIN;
        }
    };

    extern const sys::syscall_descriptor thread_create_owned_descriptor =
        thread_create_owned::descriptor;

} // namespace astia::proc::syscalls
