#include <astia/fs.hpp>
#include <astia/init.hpp>

namespace astia::fs {
    static bool step_fs(init::context&) {
        return fs::init();
    }

    static bool step_seal_boot_mounts(init::context&) {
        fs::seal_boot_mounts();

        return true;
    }
} // namespace astia::fs

ASTIA_INIT_STEP(devices, fs, astia::fs::step_fs, "heap")
ASTIA_INIT_STEP(subsystems, fs_mounts_sealed, astia::fs::step_seal_boot_mounts, "mod")
