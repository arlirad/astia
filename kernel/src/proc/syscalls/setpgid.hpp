#pragma once

#include <astia/common.hpp>

#include <syscall.hpp>
namespace astia::proc::syscall {
    i64 setpgid(i32 pid_raw, i32 pgid_raw);
}
