#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 futex(u64 address, u32 op, u32 expected, u64 timeout);
}
