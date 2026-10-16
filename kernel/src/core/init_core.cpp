#include <astia/init.hpp>

namespace astia::core {

    static auto step_core(init::context&) -> bool {
        return true;
    }

} // namespace astia::core

ASTIA_INIT_STEP(subsystems, core, astia::core::step_core, "frames")
