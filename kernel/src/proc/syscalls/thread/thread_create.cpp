#include "thread_create.hpp"
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

    i64 thread_create(u64 entry, u64 stack_top, u64 argument) {
        auto* owner = current_process();
        if (owner == nullptr || !valid_thread_start(entry, stack_top)) {
            return -EFAULT;
        }
        auto value = owner->create_thread(entry, stack_top, argument);
        return value ? static_cast<i64>(value->tid()) : -EAGAIN;
    }
}
