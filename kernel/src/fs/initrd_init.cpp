#include "filesystem.hpp"
#include "initrd_fs.hpp"
#include <astia/boot.hpp>
#include <astia/init.hpp>
#include <astia/mm/frame_allocator.hpp>

namespace astia::fs {

    static bool step_initrd(init::context& context) {
        if (context.info == nullptr)
            return false;
        for (usize i = 0; i < context.info->module_count; ++i) {
            const auto&     module = context.info->modules[i];
            initrd::archive archive;
            // The module is physical memory where the bootloader left
            // it.  Hand initrd::open the direct-map view so every file
            // payload pointer lands in the kernel half — readable under
            // any active address space, not just the kernel's own map
            // (a ring-3 spawnve reads the image through this fs).
            const void* data = mm::kernel_frames().to_virt(module.paddr);
            if (module.paddr != 0 && module.size != 0 &&
                initrd::open(data, static_cast<usize>(module.size), archive) &&
                initrd_type().attach(archive)) {
                return mount_type("initrd", "/") == 0;
            }
        }
        return true;
    }

} // namespace astia::fs

ASTIA_INIT_STEP(subsystems, initrd, astia::fs::step_initrd, "fs", "boot_info")
