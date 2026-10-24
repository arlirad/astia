#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// gettid - return the calling thread ID.
    ///
    /// Synopsis:
    ///     long gettid(void);
    i64 gettid();
}
