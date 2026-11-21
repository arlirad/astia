#include "thread_exit.hpp"

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

    i64 thread_exit(i64 result) {
        auto* self = current_thread();
        if (self == nullptr) {
            return -ESRCH;
        }
        self->exit(reinterpret_cast<void*>(static_cast<usize>(result)));
        __builtin_unreachable();
    }
}

namespace astia::proc::syscalls {

    /// thread_exit - Terminate the calling user thread.
    ///
    /// Synopsis:
    ///     long thread_exit(long result);
    ///
    /// Description:
    ///     Terminates the calling thread and records result for a joining
    ///     thread. A process becomes waitable after its last thread exits.
    ///
    /// Return Value:
    ///     Does not return.

    /// ABI ID: thread_exit.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct thread_exit : sys::syscall_handler<thread_exit, sys::syscall_id::thread_exit,
                                              sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor thread_exit_descriptor = thread_exit::descriptor;

} // namespace astia::proc::syscalls
