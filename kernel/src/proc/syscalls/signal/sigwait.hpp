#pragma once
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 sigwait(u64 set_ptr, u64 signal_ptr);
}
