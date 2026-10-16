#include <astia/boot.hpp>
#include "elf.hpp"
#include <astia/init.hpp>
#include "initrd.hpp"
#include "log.hpp"
#include "frame_allocator.hpp"
#include "module.hpp"

// The "mod" init step: turn the bootloader's module list into running code.
//
// A boot module is either a bare ELF image or an initrd (ustar tarball) that
// carries one. Bare images are loaded directly; an initrd is unpacked and every
// ELF file it contains is loaded and run. Anything that is neither is reported
// and skipped — a single bad file must not brick the boot.
//
// Depends on `frames` (the frame allocator places section contents) and
// `boot_info` (the module list itself).

namespace astia::mod {

    namespace {

        using astia::u64;
        using astia::usize;

        // Load one ELF image and run it, logging the outcome. `name` labels an
        // initrd file; when null the boot-module index `i` is used instead.
        void load_and_run(const void* data, usize size, const char* name, u64 i) {
            const module* mod = load_module(data, size);
            if (mod == nullptr) {
                if (name != nullptr) {
                    log::error("mod: %s failed to load", name);
                }
                else {
                    log::error("mod: module %llu failed to load",
                               static_cast<unsigned long long>(i));
                }
                return;
            }
            const int rc = run_module(mod);
            if (name != nullptr) {
                log::info("mod: %s rc=%d", name, rc);
            }
            else {
                log::info("mod: module %llu rc=%d", static_cast<unsigned long long>(i),
                          rc);
            }
        }

        // Per-file visitor for an initrd: load the file if it is an ELF image,
        // skip anything else (configs, data, ...).
        auto visit_initrd_file(void*, const initrd::file& f) -> bool {
            // ET_REL belongs to the kernel-module loader.  Executables
            // remain files in initrdfs and are launched by proc::spawnve.
            if (elf::kind_of(f.data, f.size) != elf::kind::relocatable) {
                return true;
            }
            load_and_run(f.data, f.size, f.name, 0);
            return true;
        }

    } // namespace

    static auto step_mod(init::context& ctx) -> bool {
        if (ctx.info == nullptr) {
            return false;
        }

        for (usize i = 0; i < ctx.info->module_count; ++i) {
            const boot::module& m = ctx.info->modules[i];
            if (m.paddr == 0 || m.size == 0) {
                continue;
            }
            // Physical memory where the bootloader left it: go through
            // the direct map so the bytes are addressable regardless of
            // which address space is active (same rule as the initrd
            // mount in fs/initrd_init.cpp).
            const void* data = mm::kernel_frames().to_virt(m.paddr);
            usize       size = static_cast<usize>(m.size);

            // A bare ELF image handed over directly.
            if (elf::kind_of(data, size) != elf::kind::none) {
                load_and_run(data, size, nullptr, i);
                continue;
            }

            // Otherwise treat it as an initrd (ustar) archive and load the ELF
            // images it carries.
            initrd::archive arc;
            if (!initrd::open(data, size, arc)) {
                log::warning("mod: module %llu is neither ELF nor initrd",
                             static_cast<unsigned long long>(i));
                continue;
            }
            log::info("mod: initrd %llu: %llu file(s)",
                      static_cast<unsigned long long>(i),
                      static_cast<unsigned long long>(initrd::count(arc)));

            initrd::each(arc, visit_initrd_file, nullptr);
        }
        return true;
    }

} // namespace astia::mod

ASTIA_INIT_STEP(subsystems, mod, astia::mod::step_mod, "boot_info", "frames")
