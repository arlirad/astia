#pragma once

#include <astia/common.hpp>

namespace astia::proc::syscall {
    i64 getrlimit(i32 resource, u64 output);
    i64 setrlimit(i32 resource, u64 input);
}
