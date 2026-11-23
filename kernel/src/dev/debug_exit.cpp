#include "debug_exit.hpp"

#include <astia/arch/platform.hpp>

namespace astia::dev {

    void debug_exit(u32 status) {
        arch::platform::debug_exit(status);
    }

} // namespace astia::dev
