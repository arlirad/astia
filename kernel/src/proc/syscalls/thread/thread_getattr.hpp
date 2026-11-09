#pragma once

#include <astia/common.hpp>

namespace astia::proc::syscall {
    i64 thread_getattr(u32 tid, u64 output);
}
