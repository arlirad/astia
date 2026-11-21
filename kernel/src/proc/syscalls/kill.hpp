#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>

namespace astia::proc::syscall {

    i64 kill(i32 pid_raw, i32 signal_raw);

} // namespace astia::proc::syscall
