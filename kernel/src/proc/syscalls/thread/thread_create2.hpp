#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 thread_create2(u64 entry, u64 stack_top, u64 argument0, u64 argument1,
                       u64 stack_base, u64 stack_size);
}
