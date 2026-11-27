#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/layout.hpp>
#include <astia/core/task.hpp>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct mprotect : sys::syscall_handler<mprotect, sys::syscall_id::mprotect,
                                           sys::syscall_mode::async> {
        static core::task<i64> invoke(u64 addr, i64 len, u32 prot) {
            auto* process = current_process();
            if (process == nullptr)
                co_return -ESRCH;

            if (len <= 0 || addr >= astia::arch::kernel_base ||
                static_cast<u64>(len) > astia::arch::kernel_base - addr)
                co_return -EINVAL;

            auto mapped_prot = mapper<map_protection>::from(prot);
            if (!mapped_prot)
                co_return -EINVAL;

            co_return process->protect(addr, static_cast<usz>(len), mapped_prot.value);
        }
    };
} // namespace astia::proc::syscalls
