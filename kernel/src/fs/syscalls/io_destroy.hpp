#pragma once
#include <astia/common.hpp>
namespace astia::fs::syscall {
    /// io_destroy - destroy an asynchronous I/O ring.
    ///
    /// Synopsis:
    ///     long io_destroy(ring *ring);
    i64 io_destroy(u64 ring);
}
