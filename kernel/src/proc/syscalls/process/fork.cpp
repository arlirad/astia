#include "fork.hpp"

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
    i64 fork(const arch::syscall_frame& frame) {
        return proc::fork(arch::caller_image(frame));
    }
}

namespace astia::proc::syscalls {

    /// fork - Create a copy-on-write child process.
    ///
    /// Synopsis:
    ///     long fork(void);
    ///
    /// Description:
    ///     Creates a child sharing copy-on-write mappings with the parent.
    ///     Descriptors and the current working directory are inherited.
    ///
    /// Return Value:
    ///     Returns the child PID in the parent and zero in the child.
    ///
    /// Errors:
    ///     -EAGAIN: A process, thread, mapping, or registry slot cannot be allocated.
    ///     -ESRCH: There is no current user process.

    /// ABI ID: fork.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct fork
        : sys::syscall_handler<fork, sys::syscall_id::fork, sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor fork_descriptor = fork::descriptor;

} // namespace astia::proc::syscalls
