#include "initrd.hpp"
#include "module.hpp"
#include <astia/boot.hpp>
#include <astia/elf.hpp>
#include <astia/init.hpp>
#include <astia/log.hpp>
#include <astia/mm/frame_allocator.hpp>

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

namespace astia::mod {

    namespace {

        using astia::u64;
        using astia::usize;

        /// This module's log source, and the initrd walk inside it.
        using logs        = astia::log::context<"mod">;
        using initrd_logs = logs::child<"initrd">;

        // Load one ELF image and run it, logging the outcome. `name` labels an
        // initrd file; when null the boot-module index `i` is used instead.
        void load_and_run(span<const u8> data, const char* name, u64 i) {
            const loaded_module* mod = load_module(data);
            if (mod == nullptr) {
                if (name != nullptr) {
                    logs::error("%s failed to load", name);
                }
                else {
                    logs::error("module %llu failed to load",
                                static_cast<unsigned long long>(i));
                }
                return;
            }
            const int rc = run_module(mod);
            if (name != nullptr) {
                logs::info("%s rc=%d", name, rc);
            }
            else {
                logs::info("module %llu rc=%d", static_cast<unsigned long long>(i), rc);
            }
        }

        // Per-file visitor for an initrd: load the file if it is an ELF image,
        // skip anything else (configs, data, ...).
        bool visit_initrd_file(void*, const initrd::file& f) {
            // ET_REL belongs to the kernel-module loader.  Executables
            // remain files in initrdfs and are launched by proc::spawnve.
            if (elf::kind_of(f.data, f.size) != elf::kind::relocatable) {
                return true;
            }
            load_and_run(span<const u8>(static_cast<const u8*>(f.data), f.size), f.name,
                         0);
            return true;
        }

    } // namespace

    static bool step_mod(init::context& ctx) {
        if (ctx.info == nullptr) {
            return false;
        }
        // The ramdisk is the fs layer's: it mounts it at / (fs/initrd_init.cpp).
        // What mod takes from it are the kernel modules it carries.
        const boot::module_info& ramdisk = ctx.info->initrd;
        if (ramdisk.paddr != 0 && ramdisk.size != 0) {
            const void*     data = mm::kernel_frames().to_virt(ramdisk.paddr);
            initrd::archive arc;
            if (initrd::open(span<const u8>(static_cast<const u8*>(data),
                                            static_cast<usize>(ramdisk.size)),
                             arc)) {
                initrd_logs::info("%llu file(s)",
                                  static_cast<unsigned long long>(initrd::count(arc)));
                initrd::each(arc, visit_initrd_file, nullptr);
            }
        }

        // Every other module the loader reported is a bare ELF image, or
        // something we do not understand.
        for (usize i = 0; i < ctx.info->module_count; ++i) {
            const boot::module_info& m = ctx.info->modules[i];
            if (m.paddr == 0 || m.size == 0) {
                continue;
            }
            // Physical memory where the bootloader left it: go through the
            // direct map so the bytes are addressable regardless of which
            // address space is active.
            const void*          data = mm::kernel_frames().to_virt(m.paddr);
            const span<const u8> image(static_cast<const u8*>(data),
                                       static_cast<usize>(m.size));

            if (elf::kind_of(image) != elf::kind::none) {
                load_and_run(image, nullptr, i);
                continue;
            }
            logs::warning("module %llu is not an ELF image",
                          static_cast<unsigned long long>(i));
        }
        return true;
    }

} // namespace astia::mod

ASTIA_INIT_STEP(devices, mod, astia::mod::step_mod, "boot_info", "frames")
