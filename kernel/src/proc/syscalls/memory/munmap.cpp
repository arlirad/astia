#include "munmap.hpp"

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

    /// ABI ID: munmap.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct munmap : sys::syscall_handler<munmap, sys::syscall_id::munmap,
                                         sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor munmap_descriptor = munmap::descriptor;

} // namespace astia::proc::syscalls
