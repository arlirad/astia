#include <astia/boot.hpp>
#include <astia/init.hpp>
#include <astia/log.hpp>
#include <astia/mm/frame_allocator.hpp>

#include "boot.hpp"
#include <astia/fs/filesystem.hpp>
#include "filesystems/initrd/initrd.hpp"

namespace astia::fs {
    namespace {
        /// This module's log source.
        using logs = astia::log::context<"fs">;
    } // namespace

    static bool step_initrd(init::context& context) {
        if (context.info == nullptr)
            return false;

        // The loader's ramdisk, split out of the module list by the boot step
        // (boot::boot_info::initrd). A boot without one has nothing to mount.
        const boot::module_info& ramdisk = context.info->initrd;

        if (ramdisk.paddr == 0 || ramdisk.size == 0)
            return true;

        // The ramdisk is physical memory where the bootloader left
        // it.  Hand the loader the direct-map view so any decompressed
        // storage and every file payload pointer land in the kernel half —
        // readable under any active address space, not just the kernel's own
        // map (a ring-3 spawnve reads the image through this fs).
        const void* data = mm::kernel_frames().to_virt(ramdisk.paddr);

        if (!initrd::load_boot_archive(data, static_cast<usz>(ramdisk.size))) {
            logs::error("initrd: %s container failed: %s",
                        initrd::container_name(initrd::boot_archive_container()),
                        initrd::decode_error_name(initrd::boot_archive_error()));

            return false;
        }

        initrd::archive archive;

        if (!initrd::open_boot_archive(archive))
            return false;

        logs::info("initrd: %s container, %llu -> %llu bytes",
                   initrd::container_name(initrd::boot_archive_container()),
                   static_cast<unsigned long long>(initrd::boot_archive_source_size()),
                   static_cast<unsigned long long>(initrd::boot_archive_stored_size()));

        return filesystems::initrd_type().attach(archive) &&
               mount_type("initrd", "/") == 0;
    }
} // namespace astia::fs

ASTIA_INIT_STEP(subsystems, initrd, astia::fs::step_initrd, "fs", "boot_info")
