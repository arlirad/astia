#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 thread_join(u32 tid, u64 result_ptr);
}
