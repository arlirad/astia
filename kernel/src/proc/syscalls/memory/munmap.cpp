#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/arch/layout.hpp>
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
    i64 munmap(u64 addr, i64 len) {
        auto* process = current_process();
        if (process == nullptr) {
            return -ESRCH;
        }
        if (len <= 0) {
            return -EINVAL;
        }
        const u64 length = static_cast<u64>(len);
        if (addr >= astia::arch::kernel_base ||
            length > astia::arch::kernel_base - addr) {
            return -EINVAL;
        }
        return process->unmap(addr, static_cast<usize>(length));
    }
}

namespace astia::proc::syscalls {

    /// munmap - Remove a mapped virtual range.
    ///
    /// Synopsis:
    ///     long munmap(void *addr, long length);
    ///
    /// Description:
    ///     Removes mappings overlapping a positive-length virtual range. The
    ///     address space may split a mapping when only part is removed.
    ///
    /// Return Value:
    ///     Returns zero on success or a negative errno.
    ///
    /// Errors:
    ///     -EINVAL: The length is not positive or the range is invalid.
    ///     -ESRCH: There is no current process.
    struct munmap
        : sys::syscall_handler<munmap, sys::syscall_id::munmap, sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            return sys::invoke_sync<&::astia::proc::syscall::munmap>(context);
        }
    };

    extern const sys::syscall_descriptor munmap_descriptor = munmap::descriptor;

} // namespace astia::proc::syscalls
