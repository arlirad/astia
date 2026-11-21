#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 waitpid(i32 pid, u64 status_ptr, u32 options);
}
