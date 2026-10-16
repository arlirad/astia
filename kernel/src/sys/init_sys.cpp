#include <astia/init.hpp>
#include <astia/sys/syscall.hpp>

namespace astia::sys {

    auto register_time_syscalls() -> bool;

    // Clock reads need only the tick counter the arch timer init step
    // starts; nothing here touches hardware, so there is no dependency
    // beyond running before userspace does (every phase does).
    static auto step_sys_time(init::context&) -> bool {
        return register_time_syscalls();
    }

} // namespace astia::sys

ASTIA_INIT_STEP(late, sys_time, astia::sys::step_sys_time)
