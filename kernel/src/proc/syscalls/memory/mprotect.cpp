#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/layout.hpp>

#include "../../mapping.hpp"
#include "../../proc.hpp"
#include "../../scheduler.hpp"

#include <syscall.hpp>

namespace astia::proc::syscalls {
    struct mprotect : sys::syscall_handler<mprotect, sys::syscall_id::mprotect,
                                           sys::syscall_mode::sync> {
        static i64 invoke(u64 addr, i64 len, u32 prot) {
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
    };

    extern const sys::syscall_descriptor mprotect_descriptor = mprotect::descriptor;

} // namespace astia::proc::syscalls
