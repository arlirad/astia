#include <astia/init.hpp>

namespace astia::core {

    static bool step_core(init::context&) {
        return true;
    }

} // namespace astia::core

ASTIA_INIT_STEP(subsystems, core, astia::core::step_core, "frames")
