#include "proc.hpp"
#include "scheduler.hpp"
#include <astia/init.hpp>

namespace astia::proc {

    bool register_syscalls();

    static bool step_proc(init::context&) {
        return proc::init() && scheduler::init() && register_syscalls();
    }

} // namespace astia::proc

ASTIA_INIT_STEP(subsystems, proc, astia::proc::step_proc, "uart16550_irq", "core")
