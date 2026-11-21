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
    struct fork
        : sys::syscall_handler<fork, sys::syscall_id::fork, sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::fork>(context);
        }
    };

    extern const sys::syscall_descriptor fork_descriptor = fork::descriptor;

} // namespace astia::proc::syscalls
