#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    /// getpgrp - return the calling process group ID.
    ///
    /// Synopsis:
    ///     long getpgrp(void);
    i64 getpgrp();
}
