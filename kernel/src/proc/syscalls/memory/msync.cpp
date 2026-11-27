#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/layout.hpp>

#include "../../proc.hpp"
#include "../../scheduler.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct msync
        : sys::syscall_handler<msync, sys::syscall_id::msync, sys::syscall_mode::async> {
        static core::task<i64> invoke(u64 addr, i64 len, u32 flags) {
            auto* process = current_process();
            if (process == nullptr)
                co_return -ESRCH;

            if (len <= 0 || addr >= astia::arch::kernel_base ||
                (static_cast<u64>(len) > astia::arch::kernel_base - addr) ||
                (addr % mm::phys_region::frame_size) != 0)
                co_return -EINVAL;

            const u32 modes = flags & (MS_SYNC | MS_ASYNC);

            if ((flags & ~(MS_SYNC | MS_ASYNC | MS_INVALIDATE)) != 0 ||
                (modes != MS_SYNC && modes != MS_ASYNC))
                co_return -EINVAL;

            auto result = co_await process->msync(addr, static_cast<usz>(len), flags);
            if (!result)
                co_return error_errno(result.error);

            co_return result.value;
        }
    };
} // namespace astia::proc::syscalls
