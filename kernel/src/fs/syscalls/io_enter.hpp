#pragma once
#include <astia/common.hpp>
namespace astia::fs::syscall {
    /// io_enter - submit and reap asynchronous I/O operations.
    ///
    /// Synopsis:
    ///     long io_enter(ring *ring, uint32_t submit, uint32_t complete,
    ///                   uint32_t flags, const timespec *timeout);
    i64 io_enter(u64 ring, u32 submit, u32 complete, u32 flags, u64 timeout);
}
