#pragma once

#include <astia/common.hpp>

namespace astia::ipc {

    using futex_operation_mask = u32;
    inline constexpr futex_operation_mask futex_private_bit = 1u << 7;

    enum class futex_operation : u32 {
        wait = 0,
        wake = 1,
    };

} // namespace astia::ipc
