#include "multiboot2.hpp"

namespace astia::boot::virt {
    namespace {

        struct [[gnu::packed]] tag_header {
            u32 type;
            u32 size;
        };
        struct [[gnu::packed]] mmap_tag {
            tag_header h;
            u32        entry_size;
            u32        version;
        };
        struct [[gnu::packed]] mmap_entry {
            u64 base;
            u64 length;
            u32 type;
            u32 reserved;
        };
        struct [[gnu::packed]] module_tag {
            tag_header h;
            u32        start;
            u32        end;
        };

        constexpr u32 tag_end      = 0;
        constexpr u32 tag_cmdline  = 1;
        constexpr u32 tag_module   = 3;
        constexpr u32 tag_mmap     = 6;
        constexpr u32 tag_acpi_old = 14;
        constexpr u32 tag_acpi_new = 15;

        constexpr usize align8(usize n) {
            return (n + 7u) & ~usize(7u);
        }

        bool range_ok(usize offset, usize length, usize total) {
            return offset <= total && length <= total - offset;
        }

        const char* terminated_string(const u8* p, usize length) {
            for (usize i = 0; i < length; ++i) {
                if (p[i] == 0)
                    return reinterpret_cast<const char*>(p);
            }
            return nullptr;
        }

    } // namespace

    bool parse_multiboot2_info(span<const u8> mbi, boot_info& out) {
        if (mbi.data() == nullptr || mbi.size() < sizeof(u32))
            return false;

        const auto* bytes   = mbi.data();
        u32         total32 = 0;
        // The fixed part is deliberately copied bytewise only after the caller has
        // supplied a non-null address; the structure is little-endian on i386.
        total32           = *reinterpret_cast<const u32*>(bytes);
        const usize total = total32;
        if (total < 16 || (total & 7u) != 0 || total > mbi.size())
            return false;

        boot_info parsed{};
        bool      saw_end        = false;
        bool      saw_mmap       = false;
        const u8* preferred_rsdp = nullptr;

        for (usize offset = 8; offset < total;) {
            if (!range_ok(offset, sizeof(tag_header), total))
                return false;
            const auto* tag = reinterpret_cast<const tag_header*>(bytes + offset);
            if (tag->size < sizeof(tag_header) || !range_ok(offset, tag->size, total))
                return false;
            const usize step = align8(tag->size);
            if (step < tag->size || !range_ok(offset, step, total))
                return false;

            if (tag->type == tag_end) {
                if (tag->size != 8)
                    return false;
                saw_end = true;
                break;
            }

            switch (tag->type) {
            case tag_cmdline: {
                const char* s = terminated_string(bytes + offset + 8, tag->size - 8);
                if (s == nullptr)
                    return false;
                parsed.cmdline = s;
                break;
            }
            case tag_module: {
                if (tag->size < sizeof(module_tag))
                    return false;
                const auto* m = reinterpret_cast<const module_tag*>(tag);
                if (m->end < m->start ||
                    terminated_string(bytes + offset + sizeof(module_tag),
                                      tag->size - sizeof(module_tag)) == nullptr) {
                    return false;
                }
                const module value{m->start, static_cast<u64>(m->end) - m->start};
                if (parsed.initrd.paddr == 0 && parsed.initrd.size == 0) {
                    parsed.initrd = value;
                }
                else if (parsed.module_count < max_modules) {
                    parsed.modules[parsed.module_count++] = value;
                }
                break;
            }
            case tag_mmap: {
                if (tag->size < sizeof(mmap_tag))
                    return false;
                const auto* map = reinterpret_cast<const mmap_tag*>(tag);
                if (map->entry_size < sizeof(mmap_entry) || (map->entry_size & 7u) != 0 ||
                    (tag->size - sizeof(mmap_tag)) % map->entry_size != 0) {
                    return false;
                }
                const usize count = (tag->size - sizeof(mmap_tag)) / map->entry_size;
                for (usize i = 0; i < count; ++i) {
                    const auto* e = reinterpret_cast<const mmap_entry*>(
                        bytes + offset + sizeof(mmap_tag) + i * map->entry_size);
                    parsed.memory.add(e->base, e->length,
                                      static_cast<mm::memory_type>(e->type));
                }
                saw_mmap = count != 0;
                break;
            }
            case tag_acpi_old:
                if (tag->size < 8 + 20)
                    return false;
                if (preferred_rsdp == nullptr)
                    preferred_rsdp = bytes + offset + 8;
                break;
            case tag_acpi_new:
                if (tag->size < 8 + 36)
                    return false;
                preferred_rsdp = bytes + offset + 8;
                break;
            default:
                break;
            }
            offset += step;
        }

        if (!saw_end || !saw_mmap)
            return false;
        if (preferred_rsdp != nullptr) {
            parsed.rsdp = reinterpret_cast<u64>(preferred_rsdp);
        }
        out = parsed;
        return true;
    }

} // namespace astia::boot::virt
