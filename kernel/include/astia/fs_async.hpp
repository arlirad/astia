#pragma once

#include <astia/core/task.hpp>
#include <astia/fs.hpp>
#include <astia/span.hpp>

namespace astia::fs {

    /// Submit a kernel-owned read/write operation. The buffer must remain a
    /// valid kernel span until the returned task completes.
    core::task<i64> read(const mm::shptr<open_file>& file, span<u8> buffer,
                         u64 offset = offset_current);
    core::task<i64> write(const mm::shptr<open_file>& file, span<const u8> buffer,
                          u64 offset = offset_current);

    /// Read exactly the requested bytes or return an error/short-read result.
    core::task<i64> read_exact(const mm::shptr<open_file>& file, span<u8> buffer,
                               u64 offset = offset_current);

} // namespace astia::fs
