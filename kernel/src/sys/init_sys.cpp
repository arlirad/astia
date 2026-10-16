#include "syscall.hpp"
#include <astia/init.hpp>

namespace astia::sys {

    bool register_time_syscalls();

    // Clock reads need only the tick counter the arch timer init step
    // starts; nothing here touches hardware, so there is no dependency
    // beyond running before userspace does (every phase does).
    static bool step_sys_time(init::context&) {
        return register_time_syscalls();
    }

} // namespace astia::sys

ASTIA_INIT_STEP(late, sys_time, astia::sys::step_sys_time)
