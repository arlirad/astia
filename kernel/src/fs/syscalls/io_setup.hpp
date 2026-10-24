#pragma once

#include <astia/common.hpp>

namespace astia::fs::syscall {
    /// io_setup - create an asynchronous I/O ring.
    ///
    /// Synopsis:
    ///     long io_setup(uint32_t entries, uint32_t flags, ring **out);
    ///
    /// Return Value:
    ///     Returns zero on success or a negative errno.
    i64 io_setup(u32 entries, u32 flags, u64 out);
}
