#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 munmap(u64 addr, i64 len);
}
