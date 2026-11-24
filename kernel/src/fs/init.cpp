#include <astia/fs.hpp>
#include <astia/init.hpp>

namespace astia::fs {

    static bool step_fs(init::context&) {
        return init();
    }

} // namespace astia::fs

ASTIA_INIT_STEP(devices, fs, astia::fs::step_fs, "heap")
