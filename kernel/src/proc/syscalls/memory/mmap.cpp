#include "mmap.hpp"
#include "../../proc.hpp"
#include "../../mapping.hpp"
#include "../../scheduler.hpp"
#include "user_mem.hpp"
#include <astia/arch/layout.hpp>
#include "syscall.hpp"
#include "../../../ipc/futex.hpp"
#include "../../../ipc/mapping.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/context.hpp>
#include <astia/arch/cpu.hpp>
#include <astia/fs.hpp>
#include <astia/log.hpp>
#include <astia/mm/heap.hpp>
namespace astia::proc::syscall {
        i64 mmap(u64 addr, i64 len, u32 prot, u32 flags, i32 fd, i64 off) {
            auto* process = current_process();
            if (process == nullptr) {
                return -ESRCH;
            }
            if (len <= 0) {
                return -EINVAL;
            }
            // The address is a userspace hint.  Never let a high-half hint
            // flow into the VMA placer (which may otherwise ignore it and
            // make boundary mistakes look like a successful mapping).
            const u64 length = static_cast<u64>(len);
            if (length > ~u64{0} - 4095u) {
                return -EINVAL;
            }
            if (addr >= astia::arch::kernel_base ||
                (addr != 0 && length > astia::arch::kernel_base - addr)) {
                return -EINVAL;
            }
            if ((flags & MAP_ANONYMOUS) != 0) {
                // The anonymous form mandates fd = -1, offset = 0.
                if (fd != -1 || off != 0) {
                    return -EINVAL;
                }
                auto mapped_prot = mapper<map_protection>::from(prot);
                auto mapped_flags = mapper<map_flags>::from(flags);
                if (!mapped_prot || !mapped_flags) return -EINVAL;
                return static_cast<i64>(process->map_anonymous(
                    addr, static_cast<usize>(length), mapped_prot.value, mapped_flags.value));
            }
            if (fd < 0 || off < 0) {
                return -EINVAL;
            }
            auto mapped_fd = mapper<fd_t>::from(fd);
            if (!mapped_fd) return -EBADF;
            auto mapped_prot = mapper<map_protection>::from(prot);
            auto mapped_flags = mapper<map_flags>::from(flags);
            if (!mapped_prot || !mapped_flags) return -EINVAL;
            return static_cast<i64>(process->map_file(
                mapped_fd.value, addr, static_cast<usize>(length), mapped_prot.value, mapped_flags.value,
                static_cast<u64>(off)));
        }
}
