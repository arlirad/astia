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

    i64 yield() {
        scheduler::yield();
        return 0;
    }
}

namespace astia::proc::syscalls {

    /// yield - Yield the current scheduling turn.
    ///
    /// Synopsis:
    ///     long yield(void);
    ///
    /// Description:
    ///     Gives another runnable thread an opportunity to run. The caller
    ///     remains runnable and may be selected again immediately.
    ///
    /// Return Value:
    ///     Returns zero.
    struct yield
        : sys::syscall_handler<yield, sys::syscall_id::yield, sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::yield>(context);
        }
    };

    extern const sys::syscall_descriptor yield_descriptor = yield::descriptor;

} // namespace astia::proc::syscalls
