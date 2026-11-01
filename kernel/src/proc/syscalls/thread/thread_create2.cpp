#include "thread_create2.hpp"
#include "thread_create_common.hpp"

#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"
#include "syscall.hpp"
#include "user_mem.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>
namespace astia::proc::syscall {

    i64 thread_create2(u64 entry, u64 stack_top, u64 argument0, u64 argument1) {
        auto* owner = current_process();
        if (owner == nullptr || !valid_thread_start(entry, stack_top) ||
            !sys::user_ptr<u8>(argument0).validate() ||
            (argument1 != 0 && !sys::user_ptr<u8>(argument1).validate())) {
            return -EFAULT;
        }
        auto value = owner->create_thread(entry, stack_top, argument0, argument1);
        return value ? static_cast<i64>(value->tid()) : -EAGAIN;
    }
}
