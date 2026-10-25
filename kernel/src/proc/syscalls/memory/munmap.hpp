#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// munmap - Remove a mapped virtual range.
    ///
    /// Synopsis:
    ///     long munmap(void *addr, long length);
    ///
    /// Description:
    ///     Removes mappings overlapping a positive-length virtual range. The
    ///     address space may split a mapping when only part is removed.
    ///
    /// Return Value:
    ///     Returns zero on success or a negative errno.
    ///
    /// Errors:
    ///     -EINVAL: The length is not positive or the range is invalid.
    ///     -ESRCH: There is no current process.
    i64 munmap(u64 addr, i64 len);
}
