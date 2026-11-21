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

    /// mmap - Map anonymous or file-backed memory.
    ///
    /// Synopsis:
    ///     long mmap(void *addr, long length, uint32_t prot, uint32_t flags, int fd, long
    ///     offset);
    ///
    /// Description:
    ///     Maps a positive-length range into the calling process. Anonymous
    ///     mappings require fd -1 and offset 0; file mappings require a valid
    ///     descriptor and non-negative offset.
    ///
    /// Return Value:
    ///     Returns the mapped base address, or a negative errno.
    ///
    /// Errors:
    ///     -EBADF: The file descriptor is invalid for a file mapping.
    ///     -EINVAL: The length, offset, protection, flags, or anonymous sentinel is
    ///     invalid. -ENOMEM: The address space or physical backing cannot be allocated.
    ///     -ENOSYS: The requested mapping mode is not implemented.
    struct mmap
        : sys::syscall_handler<mmap, sys::syscall_id::mmap, sys::syscall_mode::sync> {
        static i64 invoke(u64 addr, i64 len, u32 prot, u32 flags, i32 fd, i64 off) {
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
            const bytes_t length = static_cast<bytes_t>(len);
            if (length > ~bytes_t{0} - mm::page_mask) {
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
                auto mapped_prot  = mapper<map_protection>::from(prot);
                auto mapped_flags = mapper<map_flags>::from(flags);
                if (!mapped_prot || !mapped_flags)
                    return -EINVAL;
                return static_cast<i64>(
                    process->map_anonymous(addr, static_cast<usize>(length),
                                           mapped_prot.value, mapped_flags.value));
            }
            if (fd < 0) {
                return -EBADF;
            }
            if (off < 0) {
                return -EINVAL;
            }
            auto mapped_fd = mapper<fd_t>::from(fd);
            if (!mapped_fd)
                return -EBADF;
            auto mapped_prot  = mapper<map_protection>::from(prot);
            auto mapped_flags = mapper<map_flags>::from(flags);
            if (!mapped_prot || !mapped_flags)
                return -EINVAL;
            return static_cast<i64>(process->map_file(
                mapped_fd.value, addr, static_cast<usize>(length), mapped_prot.value,
                mapped_flags.value, static_cast<u64>(off)));
        }
    };

    extern const sys::syscall_descriptor mmap_descriptor = mmap::descriptor;

} // namespace astia::proc::syscalls
