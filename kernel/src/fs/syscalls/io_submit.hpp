#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>

/// io_submit - Submit published asynchronous I/O operations.
///
/// Synopsis:
///     long io_submit(astia_ring *ring, uint32_t count);
///
/// Return Value:
///     Returns zero or a negative errno.
namespace astia::fs::syscall {
    i64 io_submit(u64 ring, u32 submit);
}
