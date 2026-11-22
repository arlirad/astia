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

    /// thread_detach - Detach a user thread.
    ///
    /// Synopsis:
    ///     long thread_detach(uint32_t tid);
    ///
    /// Description:
    ///     Marks tid detached so its resources are reclaimed after exit and it
    ///     cannot later be joined.
    ///
    /// Return Value:
    ///     Returns zero or a negative errno.
    ///
    /// Errors:
    ///     -EINVAL: The thread already has a joiner.
    ///     -ESRCH: The thread does not exist.
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

    extern const sys::syscall_descriptor thread_detach_descriptor =
        thread_detach::descriptor;

} // namespace astia::proc::syscalls
