#include <astia/init.hpp>

#include "dl.hpp"

namespace astia::dl {
    static bool step_dl(init::context&) {
        return init();
    }
} // namespace astia::dl

ASTIA_INIT_STEP(subsystems, dl, astia::dl::step_dl, "proc")
