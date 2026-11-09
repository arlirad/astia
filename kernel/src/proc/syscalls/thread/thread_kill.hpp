#pragma once

#include <astia/common.hpp>

namespace astia::proc::syscall {

    i64 thread_kill(u32 tid, i32 signal);

}
