#pragma once

#include <astia/common.hpp>

namespace astia::ipc {

    enum class futex_operation : u32 {
        wait = 0,
        wake = 1,
    };

} // namespace astia::ipc
