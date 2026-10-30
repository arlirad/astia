#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
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
    i64 mmap(u64 addr, i64 len, u32 prot, u32 flags, i32 fd, i64 off);
}
