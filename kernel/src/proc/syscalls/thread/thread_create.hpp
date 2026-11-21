#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 thread_create(u64 entry, u64 stack_top, u64 argument);
}
