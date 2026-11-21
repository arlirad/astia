#pragma once

#include <astia/common.hpp>

namespace astia::sys {
    inline constexpr i64 realtime_epoch_sec = 1767225600;

    bool realtime_now(u64& sec, u64& nsec);
}
