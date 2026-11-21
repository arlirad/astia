#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 sigaction(i32 value, u64 next_ptr, u64 old_ptr);
}
