#pragma once
#include <astia/common.hpp>
namespace astia::fs::syscall {
    /// io_register - register asynchronous I/O resources.
    ///
    /// Synopsis:
    ///     long io_register(void);
    i64 io_register();
}
