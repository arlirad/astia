#pragma once

#include <astia/common.hpp>

namespace astia::proc::syscall {
    i64 getpgid(i64 target);
}
