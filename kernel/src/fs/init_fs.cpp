#include "fs.hpp"
#include "syscall.hpp"
#include <astia/init.hpp>

namespace astia::fs {

    bool register_syscalls();

    static bool step_fs(init::context&) {
        return init() && register_syscalls();
    }

} // namespace astia::fs

ASTIA_INIT_STEP(devices, fs, astia::fs::step_fs, "heap")
