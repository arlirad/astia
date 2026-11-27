#include <astia/init.hpp>
#include <astia/shutdown.hpp>

#include "proc.hpp"
#include "scheduler.hpp"
#include "shutdown_executor.hpp"

namespace astia::proc {
    bool register_proc_hooks();

    static bool step_proc(init::context&) {
        if (!proc::init() || !scheduler::init())
            return false;

        if (!shutdown::detail::set_dispatch_executor(&shutdown_executor))
            return false;

        return register_proc_hooks();
    }
} // namespace astia::proc

ASTIA_INIT_STEP(subsystems, proc, astia::proc::step_proc, "uart16550_irq", "core")
