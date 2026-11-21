#pragma once
#include <astia/arch/context.hpp>
#include <astia/common.hpp>
namespace astia::proc::syscall {
    i64 fork(const arch::syscall_frame& frame);
}
