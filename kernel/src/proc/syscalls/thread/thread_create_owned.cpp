#include <astia/api/errno.h>

#include "../../proc.hpp"
#include "thread_create_common.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct thread_create_owned
        : sys::syscall_handler<thread_create_owned, sys::syscall_id::thread_create_owned,
                               sys::syscall_mode::sync> {
        static i64 invoke(sys::user_ptr<u8> entry, u64 stack_top,
                          sys::user_ptr<u8> argument0, sys::user_ptr<u8> argument1,
                          sys::user_ptr<u8> stack_base, u64 stack_size) {
            auto* owner = current_process();
            if (owner == nullptr ||
                !detail::valid_thread_start(entry.address(), stack_top) ||
                !argument0.validate() || (!argument1.null() && !argument1.validate()) ||
                ((!stack_base.null() || stack_size != 0) &&
                 !detail::valid_thread_stack_metadata(stack_base.address(), stack_size,
                                                      stack_top)))
                return -EFAULT;
            auto value = owner->create_thread(
                entry.address(), stack_top, argument0.address(), argument1.address(),
                true, stack_base.address(), static_cast<usize>(stack_size));
            return value ? static_cast<i64>(value->tid()) : -EAGAIN;
        }
    };

    extern const sys::syscall_descriptor thread_create_owned_descriptor =
        thread_create_owned::descriptor;

} // namespace astia::proc::syscalls
