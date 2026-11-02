// x86/HVM direct boot ABI ("PVH") start_info parsing.
//
// Layout taken verbatim from Xen's public header:
//   xen/include/public/arch-x86/hvm/start_info.h
// The magic is "xEn3" with bit 7 of the third byte set.

#include "pvh.hpp"

namespace astia::boot::virt {

    namespace {

        constexpr u32 hvm_start_magic = 0x336EC578u;

        struct hvm_start_info {
            u32 magic;
            u32 version;
            u32 flags;
            u32 nr_modules;
            u64 modlist_paddr;
            u64 cmdline_paddr;
            u64 rsdp_paddr;
            // Only present from version 1 onward.
            u64 memmap_paddr;
            u32 memmap_entries;
            u32 reserved;
        };

        struct hvm_memmap_table_entry {
            u64 addr;
            u64 size;
            u32 type;
            u32 reserved;
        };

        struct hvm_modlist_entry {
            u64 paddr;
            u64 size;
            u64 cmdline_paddr;
            u64 reserved;
        };

        static_assert(sizeof(hvm_start_info) == 56, "PVH start_info layout");
        static_assert(sizeof(hvm_memmap_table_entry) == 24, "PVH memmap entry layout");
        static_assert(sizeof(hvm_modlist_entry) == 32, "PVH modlist entry layout");

    } // namespace

    bool parse_start_info(span<const u8> start_info, boot_info& out) {
        if (start_info.data() == nullptr || start_info.size() < sizeof(hvm_start_info)) {
            return false;
        }

        const auto* info = reinterpret_cast<const hvm_start_info*>(start_info.data());
        if (info->magic != hvm_start_magic) {
            return false;
        }
        if (info->version < 1 || info->memmap_entries == 0) {
            return false;
        }

        out = boot_info{};

        const auto* entries =
            reinterpret_cast<const hvm_memmap_table_entry*>(info->memmap_paddr);
        for (u32 i = 0; i < info->memmap_entries; ++i) {
            out.memory.add(entries[i].addr, entries[i].size,
                           static_cast<mm::memory_type>(entries[i].type));
        }

        if (info->cmdline_paddr != 0) {
            out.cmdline = reinterpret_cast<const char*>(info->cmdline_paddr);
        }
        out.rsdp = info->rsdp_paddr;

        if (info->modlist_paddr != 0 && info->nr_modules != 0) {
            const auto* modules =
                reinterpret_cast<const hvm_modlist_entry*>(info->modlist_paddr);
            // The ABI's convention: the first module is the ramdisk and
            // everything after it is a module.  Linux reads the handoff the
            // same way (arch/x86/platform/pvh/enlighten.c).
            out.initrd = {modules[0].paddr, modules[0].size};
            for (u32 i = 1; i < info->nr_modules && out.module_count < max_modules; ++i) {
                out.modules[out.module_count++] = {modules[i].paddr, modules[i].size};
            }
        }
        return true;
    }

} // namespace astia::boot::virt
