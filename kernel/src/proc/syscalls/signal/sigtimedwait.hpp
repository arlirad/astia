#pragma once

#include <astia/common.hpp>

namespace astia::proc::syscall {
    i64 sigtimedwait(u64 set_ptr, u64 info_ptr, u64 timeout_ptr);
}
