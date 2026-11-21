#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 mmap(u64 addr, i64 len, u32 prot, u32 flags, i32 fd, i64 off);
}
