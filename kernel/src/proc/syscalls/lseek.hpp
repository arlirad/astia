#pragma once

#include <astia/common.hpp>

namespace astia::proc::syscall {
    i64 lseek(i64 fd, i64 offset, i64 whence);
}
