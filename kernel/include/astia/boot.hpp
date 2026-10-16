#pragma once

#include <astia/boot/types.hpp>
#include <astia/common.hpp>
#include <astia/mm/memory_map.hpp>

namespace astia::boot {

    /// Maximum number of loader modules kept in boot_info. Extra modules the
    /// loader reports are dropped, mirroring memory_map's fixed capacity.
    inline constexpr usize max_modules = 8;

    /// One module (initrd, ramdisk, ...) the loader handed to the kernel,
    /// described by physical address and size.
    struct module {
        u64 paddr = 0;
        u64 size  = 0;
    };

    /// Everything the boot loader communicates to the kernel, in a
    /// bootloader-agnostic form. Protocol-specific loaders (PVH today) translate
    /// their native handoff into this struct; every later consumer sees only
    /// this type, never the raw loader structures.
    ///
    /// Lifetime: produced by the boot init step into static storage, so the
    /// pointer carried in init::context stays valid for the whole life of the
    /// kernel. Fields that point into early memory (`cmdline`) are only valid
    /// while the identity map holds; copy them before unmapping.
    struct boot_info {
        mm::memory_map memory;            ///< Physical memory map.
        const char*    cmdline = nullptr; ///< NUL-terminated command line, or null.
        u64            rsdp    = 0;       ///< ACPI RSDP physical address, 0 if absent.
        module         modules[max_modules]{}; ///< Loader modules.
        usize          module_count = 0;       ///< Number of valid entries in modules.
    };

} // namespace astia::boot
