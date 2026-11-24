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
#include "user_mem.hpp"

#include <syscall.hpp>
namespace astia::proc::syscalls {

    struct thread_detach
        : sys::syscall_handler<thread_detach, sys::syscall_id::thread_detach,
                               sys::syscall_mode::sync> {
        static i64 invoke(u32 tid) {
            auto* owner = current_process();
            if (owner == nullptr) {
                return -ESRCH;
            }
            auto target = owner->thread_by_tid(tid);
            return target ? target->detach() : -ESRCH;
        }
    };

} // namespace astia::proc::syscalls
