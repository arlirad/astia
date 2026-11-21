#include <astia/init.hpp>

#include "proc.hpp"
#include "scheduler.hpp"

namespace astia::proc {

    bool register_proc_hooks();

    static bool step_proc(init::context&) {
        return proc::init() && scheduler::init() && register_proc_hooks();
    }

} // namespace astia::proc

ASTIA_INIT_STEP(subsystems, proc, astia::proc::step_proc, "uart16550_irq", "core")
