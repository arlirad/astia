#pragma once

#include <astia/common.hpp>

namespace astia::dev {
    /// Forward one layout-mapped byte to the virtual terminal input path.
    /// The module owns scan-code decoding and layout; the kernel owns the
    /// terminal line discipline.
    bool tty_input_write(u8 byte);
} // namespace astia::dev
