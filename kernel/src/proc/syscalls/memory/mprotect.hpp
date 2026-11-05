#pragma once

#include <astia/common.hpp>

namespace astia::proc::syscall {
    i64 mprotect(u64 addr, i64 len, u32 prot);
}
