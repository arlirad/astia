#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 sigsuspend(u64 mask_ptr);
}
