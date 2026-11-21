#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "thread_create_common.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {

    i64 thread_create_owned(u64 entry, u64 stack_top, u64 argument0, u64 argument1,
                            u64 stack_base, u64 stack_size) {
        auto* owner = current_process();
        if (owner == nullptr || !valid_thread_start(entry, stack_top) ||
            !sys::user_ptr<u8>(argument0).validate() ||
            (argument1 != 0 && !sys::user_ptr<u8>(argument1).validate()) ||
            ((stack_base != 0 || stack_size != 0) &&
             !valid_thread_stack_metadata(stack_base, stack_size, stack_top)))
            return -EFAULT;
        auto value = owner->create_thread(entry, stack_top, argument0, argument1, true,
                                          stack_base, static_cast<usize>(stack_size));
        return value ? static_cast<i64>(value->tid()) : -EAGAIN;
    }

}

namespace astia::proc::syscalls {
    struct thread_create_owned
        : sys::syscall_handler<thread_create_owned, sys::syscall_id::thread_create_owned,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::thread_create_owned>(
                context);
        }
    };

    extern const sys::syscall_descriptor thread_create_owned_descriptor =
        thread_create_owned::descriptor;

} // namespace astia::proc::syscalls
