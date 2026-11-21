#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 spawnve(u64 path_ptr, u64 argv_ptr, u64 envp_ptr);
}
