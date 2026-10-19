#pragma once

#include <astia/common.hpp>

namespace astia::dbg {

    /// Print a bounded frame-pointer backtrace. Addresses are deliberately
    /// printed raw so this remains useful before symbol metadata exists.
    void stacktrace(u64 rbp, u64 rip);

} // namespace astia::dbg
