// x86/HVM direct boot ABI ("PVH") start_info parsing.
//
// Layout taken verbatim from Xen's public header:
//   xen/include/public/arch-x86/hvm/start_info.h
// The magic is "xEn3" with bit 7 of the third byte set.

#include <astia/boot/virt/pvh.hpp>

namespace astia::boot::virt {

    namespace {

        constexpr u32 hvm_start_magic = 0x336EC578u;

        constexpr usize start_info_size = 56;
        constexpr usize memmap_entry_size = 24;
        constexpr usize modlist_entry_size = 32;

        template <typename T>
        T load(const u8* bytes) {
            T value{};
            __builtin_memcpy(&value, bytes, sizeof(value));
            return value;
        }

        bool range_ok(span<const u8> memory, u64 address, u64 length) {
            const u64 base = memory.address();
            const u64 size = static_cast<u64>(memory.size());
            return address >= base && address - base <= size &&
                   length <= size - (address - base);
        }

        const u8* address_ptr(span<const u8> memory, u64 address, usize length) {
            if (!range_ok(memory, address, length))
                return nullptr;
            return reinterpret_cast<const u8*>(address);
        }

        const char* terminated_string(span<const u8> memory, u64 address) {
            if (!range_ok(memory, address, 1))
                return nullptr;
            const u8* bytes = reinterpret_cast<const u8*>(address);
            const usize length = memory.size() - static_cast<usize>(address - memory.address());
            for (usize i = 0; i < length; ++i) {
                if (bytes[i] == 0)
                    return reinterpret_cast<const char*>(bytes);
            }
            return nullptr;
        }

        bool descriptor_range(span<const u8> memory, u64 address, u64 count,
                              usize entry_size, const u8*& table) {
            if (count > static_cast<u64>(-1) / entry_size)
                return false;
            const u64 length = count * entry_size;
            if (length > static_cast<u64>(static_cast<usize>(-1)) ||
                !range_ok(memory, address, length))
                return false;
            table = address_ptr(memory, address, static_cast<usize>(length));
            return table != nullptr || length == 0;
        }

        bool module_range(span<const u8> memory, u64 address, u64 length) {
            return range_ok(memory, address, length);
        }

    } // namespace

    bool parse_start_info(span<const u8> start_info, span<const u8> physical_memory,
                          boot_info& out) {
        if (start_info.data() == nullptr || start_info.size() < start_info_size) {
            return false;
        }

        const u8* bytes = start_info.data();
        const u32 magic = load<u32>(bytes);
        const u32 version = load<u32>(bytes + 4);
        const u32 module_count = load<u32>(bytes + 12);
        const u64 module_list = load<u64>(bytes + 16);
        const u64 cmdline = load<u64>(bytes + 24);
        const u64 rsdp = load<u64>(bytes + 32);
        const u64 memory_map = load<u64>(bytes + 40);
        const u32 memory_map_count = load<u32>(bytes + 48);
        if (magic != hvm_start_magic) {
            return false;
        }
        if (version < 1 || memory_map_count == 0) {
            return false;
        }

        const u8* entries = nullptr;
        if (!descriptor_range(physical_memory, memory_map, memory_map_count,
                              memmap_entry_size, entries))
            return false;
        const u8* modules = nullptr;
        if (module_count != 0 &&
            !descriptor_range(physical_memory, module_list, module_count,
                              modlist_entry_size, modules))
            return false;

        boot_info parsed{};
        for (u32 i = 0; i < memory_map_count; ++i) {
            const u8* entry = entries + static_cast<usize>(i) * memmap_entry_size;
            parsed.memory.add(load<u64>(entry), load<u64>(entry + 8),
                              static_cast<mm::memory_type>(load<u32>(entry + 16)));
        }

        if (cmdline != 0) {
            parsed.cmdline = terminated_string(physical_memory, cmdline);
            if (parsed.cmdline == nullptr)
                return false;
        }
        parsed.rsdp = rsdp;

        if (module_count != 0) {
            // The ABI's convention: the first module is the ramdisk and
            // everything after it is a module.  Linux reads the handoff the
            // same way (arch/x86/platform/pvh/enlighten.c).
            const u64 initrd_address = load<u64>(modules);
            const u64 initrd_size = load<u64>(modules + 8);
            if (!module_range(physical_memory, initrd_address, initrd_size))
                return false;
            parsed.initrd = {initrd_address, initrd_size};
            for (u32 i = 1; i < module_count && parsed.module_count < max_modules; ++i) {
                const u8* module = modules + static_cast<usize>(i) * modlist_entry_size;
                const u64 address = load<u64>(module);
                const u64 size = load<u64>(module + 8);
                if (!module_range(physical_memory, address, size))
                    return false;
                parsed.modules[parsed.module_count++] = {address, size};
            }
        }
        out = parsed;
        return true;
    }

    bool parse_start_info(span<const u8> start_info, boot_info& out) {
        return parse_start_info(start_info, start_info, out);
    }

} // namespace astia::boot::virt
