#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/layout.hpp>

#include "../../proc.hpp"
#include "../../scheduler.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct msync
        : sys::syscall_handler<msync, sys::syscall_id::msync, sys::syscall_mode::sync> {
        static i64 invoke(sys::syscall_context& context) {
            u64   addr    = static_cast<u64>(context.arguments[0]);
            i64   len     = static_cast<i64>(context.arguments[1]);
            u32   flags   = static_cast<u32>(context.arguments[2]);
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
    };

    extern const sys::syscall_descriptor msync_descriptor = msync::descriptor;

} // namespace astia::proc::syscalls
