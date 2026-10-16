#include "fs.hpp"
#include <astia/init.hpp>
#include "syscall.hpp"

namespace astia::fs {

    auto register_syscalls() -> bool;

    static auto step_fs(init::context&) -> bool {
        return init() && register_syscalls();
    }

} // namespace astia::fs

ASTIA_INIT_STEP(devices, fs, astia::fs::step_fs, "heap")
