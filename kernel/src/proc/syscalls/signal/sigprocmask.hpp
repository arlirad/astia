#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 sigprocmask(i32 how, u32 set, u64 old_ptr);
}
