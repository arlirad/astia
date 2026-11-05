#pragma once

#include <astia/common.hpp>

namespace astia::proc::syscall {
    i64 sigaltstack(u64 stack_ptr, u64 old_stack_ptr);
}
