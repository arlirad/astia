#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/layout.hpp>

#include "../../proc.hpp"
#include "../../scheduler.hpp"

#include <syscall.hpp>

namespace astia::proc::syscall {
    i64 msync(u64 addr, i64 len, u32 flags) {
        auto* process = current_process();
        if (process == nullptr) {
            return -ESRCH;
        }
        if (len <= 0 || addr >= astia::arch::kernel_base ||
            (static_cast<u64>(len) > astia::arch::kernel_base - addr) ||
            (addr % mm::phys_region::frame_size) != 0) {
            return -EINVAL;
        }
        const u32 modes = flags & (MS_SYNC | MS_ASYNC);
        if ((flags & ~(MS_SYNC | MS_ASYNC | MS_INVALIDATE)) != 0 ||
            (modes != MS_SYNC && modes != MS_ASYNC)) {
            return -EINVAL;
        }
        return process->msync(addr, static_cast<usize>(len), flags);
    }
}

namespace astia::proc::syscalls {
    struct msync
        : sys::syscall_handler<msync, sys::syscall_id::msync, sys::syscall_mode::async> {
        static core::task<i64> invoke(sys::syscall_context& context) {
            co_return sys::invoke_sync<&::astia::proc::syscall::msync>(context);
        }
    };

    extern const sys::syscall_descriptor msync_descriptor = msync::descriptor;

} // namespace astia::proc::syscalls
