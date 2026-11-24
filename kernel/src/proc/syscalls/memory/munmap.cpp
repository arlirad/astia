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

#include <syscall.hpp>
namespace astia::proc::syscalls {

    struct munmap
        : sys::syscall_handler<munmap, sys::syscall_id::munmap, sys::syscall_mode::sync> {
        static i64 invoke(u64 addr, i64 len) {
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
    };

} // namespace astia::proc::syscalls
