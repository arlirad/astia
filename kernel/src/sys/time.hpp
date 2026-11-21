#pragma once

#include <astia/common.hpp>

namespace astia::sys {
    i64  sys_clock_gettime(i32 clock_id, u64 out);
    i64  sys_clock_getres(i32 clock_id, u64 out);
    bool realtime_now(u64& sec, u64& nsec);
}
