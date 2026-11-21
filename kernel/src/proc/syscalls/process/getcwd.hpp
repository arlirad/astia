#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 getcwd(u64 buffer, u64 length);
}
