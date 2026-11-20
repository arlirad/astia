#include "mprotect.hpp"

#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/layout.hpp>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {
    i64 mprotect(u64 addr, i64 len, u32 prot) {
        auto* process = current_process();
        if (process == nullptr) {
            return -ESRCH;
        }
        if (len <= 0 || addr >= astia::arch::kernel_base ||
            static_cast<u64>(len) > astia::arch::kernel_base - addr) {
            return -EINVAL;
        }
        auto mapped_prot = mapper<map_protection>::from(prot);
        if (!mapped_prot) {
            return -EINVAL;
        }
        return process->protect(addr, static_cast<usize>(len), mapped_prot.value);
    }
}

namespace astia::proc::syscalls {

    /// ABI ID: mprotect.
    /// Sync/async mode: async.
    /// Arguments: six ABI register words are available through syscall_context.
    /// Result: a signed i64 result or a negative errno.
    /// Ownership: proc subsystem.
    /// Errors: invalid or unavailable operations return the owning implementation's
    /// negative errno.
    struct mprotect : sys::syscall_handler<mprotect, sys::syscall_id::mprotect,
                                           sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::legacy_dispatch(context);
        }
    };

    extern const sys::syscall_descriptor mprotect_descriptor = mprotect::descriptor;

} // namespace astia::proc::syscalls
