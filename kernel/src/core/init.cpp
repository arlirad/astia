#include <astia/core/resume_queue.hpp>
#include <astia/init.hpp>

namespace astia::core {
    static bool step_core(init::context&) {
        initialize_resume_queues();

        return true;
    }
} // namespace astia::core

ASTIA_INIT_STEP(subsystems, core, astia::core::step_core, "frames")
