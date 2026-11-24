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

    struct thread_create
        : sys::syscall_handler<thread_create, sys::syscall_id::thread_create,
                               sys::syscall_mode::sync> {
        static i64 invoke(sys::user_ptr<u8> entry, u64 stack_top,
                          sys::user_ptr<u8> argument) {
            auto* owner = current_process();
            if (owner == nullptr ||
                !detail::valid_thread_start(entry.address(), stack_top)) {
                return -EFAULT;
            }
            auto value =
                owner->create_thread(entry.address(), stack_top, argument.address());
            return value ? static_cast<i64>(value->tid()) : -EAGAIN;
        }
    };

} // namespace astia::proc::syscalls
