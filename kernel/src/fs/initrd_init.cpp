#include "filesystem.hpp"
#include "initrd_fs.hpp"
#include <astia/boot.hpp>
#include <astia/init.hpp>
#include <astia/log.hpp>
#include <astia/mm/frame_allocator.hpp>

namespace astia::fs {

    namespace {
        /// This module's log source.
        using logs = astia::log::context<"fs">;
    } // namespace

    static bool step_initrd(init::context& context) {
        if (context.info == nullptr) {
            return false;
        }

        // The loader's ramdisk, split out of the module list by the boot step
        // (boot::boot_info::initrd). A boot without one has nothing to mount.
        const boot::module& ramdisk = context.info->initrd;
        if (ramdisk.paddr == 0 || ramdisk.size == 0) {
            return true;
        }

        initrd::archive archive;
        // The ramdisk is physical memory where the bootloader left
        // it.  Hand initrd::open the direct-map view so every file
        // payload pointer lands in the kernel half — readable under
        // any active address space, not just the kernel's own map
        // (a ring-3 spawnve reads the image through this fs).
        const void* data = mm::kernel_frames().to_virt(ramdisk.paddr);
        if (!initrd::open(span<const u8>(static_cast<const u8*>(data),
                                         static_cast<usize>(ramdisk.size)),
                          archive)) {
            logs::error("ramdisk is not a ustar archive");
            return false;
        }
        return initrd_type().attach(archive) && mount_type("initrd", "/") == 0;
    }

} // namespace astia::fs

ASTIA_INIT_STEP(subsystems, initrd, astia::fs::step_initrd, "fs", "boot_info")
