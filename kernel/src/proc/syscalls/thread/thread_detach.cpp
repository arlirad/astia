#include "thread_detach.hpp"

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
namespace astia::proc::syscall {

    i64 thread_detach(u32 tid) {
        auto* owner = current_process();
        if (owner == nullptr) {
            return -ESRCH;
        }
        auto target = owner->thread_by_tid(tid);
        return target ? target->detach() : -ESRCH;
    }
}

namespace astia::proc::syscalls {

    /// ABI ID: thread_detach.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct thread_detach
        : sys::syscall_handler<thread_detach, sys::syscall_id::thread_detach,
                               sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor thread_detach_descriptor =
        thread_detach::descriptor;

} // namespace astia::proc::syscalls
