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
    struct mmap
        : sys::syscall_handler<mmap, sys::syscall_id::mmap, sys::syscall_mode::async> {
        static core::task<i64> invoke(u64 addr, i64 len, u32 prot, u32 flags, i32 fd,
                                      i64 off) {
            auto* process = current_process();
            if (process == nullptr)
                co_return -ESRCH;

            if (len <= 0)
                co_return -EINVAL;

            // The address is a userspace hint.  Never let a high-half hint
            // flow into the VMA placer (which may otherwise ignore it and
            // make boundary mistakes look like a successful mapping).
            const bytes_t length = static_cast<bytes_t>(len);

            if (length > ~bytes_t{0} - mm::page_mask)
                co_return -EINVAL;

            if (addr >= astia::arch::kernel_base ||
                (addr != 0 && length > astia::arch::kernel_base - addr))
                co_return -EINVAL;

            if ((flags & MAP_ANONYMOUS) != 0) {
                // The anonymous form mandates fd = -1, offset = 0.
                if (fd != -1 || off != 0)
                    co_return -EINVAL;

                auto mapped_prot  = mapper<map_protection>::from(prot);
                auto mapped_flags = mapper<map_flags>::from(flags);

                if (!mapped_prot || !mapped_flags)
                    co_return -EINVAL;

                co_return static_cast<i64>(
                    process->map_anonymous(addr, static_cast<usz>(length),
                                           mapped_prot.value, mapped_flags.value));
            }

            if (fd < 0)
                co_return -EBADF;

            if (off < 0)
                co_return -EINVAL;

            auto mapped_fd = mapper<fd_t>::from(fd);
            if (!mapped_fd)
                co_return -EBADF;

            auto mapped_prot  = mapper<map_protection>::from(prot);
            auto mapped_flags = mapper<map_flags>::from(flags);

            if (!mapped_prot || !mapped_flags)
                co_return -EINVAL;

            auto mapped = co_await process->map_file(
                mapped_fd.value, addr, static_cast<usz>(length), mapped_prot.value,
                mapped_flags.value, static_cast<u64>(off));
            if (!mapped)
                co_return error_errno(mapped.error);

            co_return static_cast<i64>(mapped.value);
        }
    };
} // namespace astia::proc::syscalls
