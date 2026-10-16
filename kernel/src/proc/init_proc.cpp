#include <astia/init.hpp>
#include "proc.hpp"
#include "scheduler.hpp"

namespace astia::proc {

    auto register_syscalls() -> bool;

    static auto step_proc(init::context&) -> bool {
        return proc::init() && scheduler::init() && register_syscalls();
    }

} // namespace astia::proc

ASTIA_INIT_STEP(subsystems, proc, astia::proc::step_proc, "uart16550_irq", "core")
