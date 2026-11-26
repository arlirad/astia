#include <astia/boot.hpp>
#include <astia/elf.hpp>
#include <astia/fs.hpp>
#include <astia/init.hpp>
#include <astia/log.hpp>
#include <astia/mm/frame_allocator.hpp>

#include "boot.hpp"
#include "initrd.hpp"
#include "module.hpp"

// The "mod" init step: turn the bootloader's handoff into running code.
//
// The ramdisk the loader hands over (boot::boot_info::initrd) is a cpio
// archive: every ET_REL file it carries is a kernel module, and each of those
// is loaded and run. Modules besides the ramdisk are bare ELF images, loaded
// directly. Anything that is not an ELF image is reported and skipped — a
// single bad module must not brick the boot.
//
// Depends on `frames` (the frame allocator places section contents) and
// `boot_info` (the ramdisk and the module list).

extern unsigned char astia_devfs_module_image[];
extern unsigned int  astia_devfs_module_image_len;

namespace astia::mod {
    namespace {
        using astia::u64;
        using astia::usize;

        /// This module's log source, and the initrd walk inside it.
        using logs        = astia::log::context<"mod">;
        using initrd_logs = logs::child<"initrd">;

        // Load one ELF image and run it, logging the outcome. `name` labels an
        // initrd file; when null the boot-module index `i` is used instead.
        // Boot modules are resident: once their entrypoint (and any driver
        // registration it performed) is live they can never be unloaded.
        void load_and_run(span<const u8> data, const char* name, u64 i) {
            module_load_options options{};

            options.resident = true;

            module_id id{};

            if (load_image(data, options, id) != module_error::ok) {
                if (name != nullptr)
                    logs::error("%s failed to load", name);
                else
                    logs::error("module %llu failed to load",
                                static_cast<unsigned long long>(i));

                return;
            }

            int rc = 0;

            if (run(id, rc) != module_error::ok)
                rc = -1;

            if (name != nullptr)
                logs::info("%s rc=%d", name, rc);
            else
                logs::info("module %llu rc=%d", static_cast<unsigned long long>(i), rc);
        }

        // Per-file visitor for an initrd: load the file if it is an ELF image,
        // skip anything else (configs, data, ...).
        bool load_required_devfs() {
            module_load_options options{};

            options.resident = true;

            constexpr char name[] = "devfs";

            for (usize i = 0; i + 1 < sizeof options.name && name[i] != '\0'; ++i) {
                options.name[i] = name[i];
            }

            module_id id{};

            const span<const u8> image(static_cast<const u8*>(astia_devfs_module_image),
                                       static_cast<usize>(astia_devfs_module_image_len));

            if (load_image(image, options, id) != module_error::ok) {
                logs::error("required devfs module failed to load");

                return false;
            }

            int rc = 0;

            if (run(id, rc) != module_error::ok || rc != 0) {
                logs::error("required devfs module failed rc=%d", rc);

                return false;
            }

            if (!astia::fs::device_namespace_ready()) {
                logs::error("required devfs module did not mount /dev");

                return false;
            }

            logs::info("devfs resident rc=%d", rc);

            return true;
        }

        bool visit_initrd_file(void*, const initrd::file& f) {
            // ET_REL belongs to the kernel-module loader.  Executables
            // remain files in initrdfs and are launched by proc::spawnve.
            if (elf::loader::kind_of(f.data, f.size) != elf::loader::kind::relocatable)
                return true;

            load_and_run(span<const u8>(static_cast<const u8*>(f.data), f.size), f.name,
                         0);

            return true;
        }
    } // namespace

    static bool step_mod(init::context& ctx) {
        if (!ctx.info)
            return false;

        if (!load_required_devfs())
            return false;

        // This step runs before the fs mount, so it loads and publishes the
        // normalized archive first; fs/initrd_init.cpp later reuses it for
        // the root mount instead of decoding or opening the image again.
        const boot::module_info& ramdisk = ctx.info->initrd;

        if (ramdisk.paddr != 0 && ramdisk.size != 0) {
            const void* data = mm::kernel_frames().to_virt(ramdisk.paddr);

            if (!initrd::load_boot_archive(data, static_cast<usize>(ramdisk.size))) {
                initrd_logs::error(
                    "%s container failed: %s",
                    initrd::container_name(initrd::boot_archive_container()),
                    initrd::decode_error_name(initrd::boot_archive_error()));

                return false;
            }
        }

        initrd::archive arc;

        if (initrd::boot_archive_ready() && initrd::open_boot_archive(arc)) {
            initrd_logs::info("%llu file(s)",
                              static_cast<unsigned long long>(initrd::count(arc)));
            initrd::each(arc, visit_initrd_file, nullptr);
        }

        // Every other module the loader reported is a bare ELF image, or
        // something we do not understand.
        for (usize i = 0; i < ctx.info->module_count; ++i) {
            const boot::module_info& m = ctx.info->modules[i];

            if (m.paddr == 0 || m.size == 0)
                continue;

            // Physical memory where the bootloader left it: go through the
            // direct map so the bytes are addressable regardless of which
            // address space is active.
            const void*          data = mm::kernel_frames().to_virt(m.paddr);
            const span<const u8> image(static_cast<const u8*>(data),
                                       static_cast<usize>(m.size));

            if (elf::loader::kind_of(image) != elf::loader::kind::none) {
                load_and_run(image, nullptr, i);
                continue;
            }

            logs::warning("module %llu is not an ELF image",
                          static_cast<unsigned long long>(i));
        }

        return true;
    }
} // namespace astia::mod

ASTIA_INIT_STEP(devices, mod, astia::mod::step_mod, "boot_info", "frames", "fs")
