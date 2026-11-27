#include <astia/boot/virt/multiboot2.hpp>

namespace astia::boot::virt {
    namespace {
        constexpr u32 tag_end         = 0;
        constexpr u32 tag_cmdline     = 1;
        constexpr u32 tag_module      = 3;
        constexpr u32 tag_framebuffer = 8;
        constexpr u32 tag_mmap        = 6;
        constexpr u32 tag_acpi_old    = 14;
        constexpr u32 tag_acpi_new    = 15;

        constexpr usz align8(usz n) {
            return n > static_cast<usz>(-1) - 7u ? 0 : (n + 7u) & ~usz(7u);
        }

        bool range_ok(usz offset, usz length, usz total) {
            return offset <= total && length <= total - offset;
        }

        const char* terminated_string(const u8* p, usz length) {
            for (usz i = 0; i < length; ++i) {
                if (p[i] == 0)
                    return reinterpret_cast<const char*>(p);
            }

            return nullptr;
        }

        template <typename T>
        T load(const u8* bytes) {
            T value{};

            __builtin_memcpy(&value, bytes, sizeof(value));

            return value;
        }
    } // namespace

    bool parse_multiboot2_info(span<const u8> mbi, boot_info& out) {
        if (mbi.data() == nullptr || mbi.size() < sizeof(u32))
            return false;

        const auto* bytes = mbi.data();

        const u32 total32 = load<u32>(bytes);
        const usz total   = total32;

        if (total < 16 || (total & 7u) != 0 || total > mbi.size())
            return false;

        boot_info parsed{};

        bool saw_end  = false;
        bool saw_mmap = false;

        const u8* preferred_rsdp = nullptr;

        for (usz offset = 8; offset < total;) {
            if (!range_ok(offset, 8, total))
                return false;

            const u32 type = load<u32>(bytes + offset);
            const u32 size = load<u32>(bytes + offset + sizeof(u32));

            if (size < 8 || !range_ok(offset, size, total))
                return false;

            const usz step = align8(size);

            if (step < size || step == 0 || !range_ok(offset, step, total))
                return false;

            if (type == tag_end) {
                if (size != 8)
                    return false;

                saw_end = true;

                break;
            }

            switch (type) {
            case tag_cmdline: {
                const char* s = terminated_string(bytes + offset + 8, size - 8);
                if (s == nullptr)
                    return false;

                parsed.cmdline = s;

                break;
            }
            case tag_module: {
                if (size < 16)
                    return false;

                const u32 start = load<u32>(bytes + offset + 8);
                const u32 end   = load<u32>(bytes + offset + 12);

                if (end < start ||
                    terminated_string(bytes + offset + 16, size - 16) == nullptr)
                    return false;

                const module_info value{start, static_cast<u64>(end) - start};

                if (parsed.initrd.paddr == 0 && parsed.initrd.size == 0)
                    parsed.initrd = value;
                else if (parsed.module_count < max_modules)
                    parsed.modules[parsed.module_count++] = value;

                break;
            }
            case tag_mmap: {
                if (size < 16)
                    return false;

                const u32 entry_size = load<u32>(bytes + offset + 8);

                if (entry_size < 24 || (entry_size & 7u) != 0 ||
                    (size - 16) % entry_size != 0)
                    return false;

                const usz count = (size - 16) / entry_size;

                for (usz i = 0; i < count; ++i) {
                    const u8* entry = bytes + offset + 16 + i * entry_size;

                    const u64 base       = load<u64>(entry);
                    const u64 length     = load<u64>(entry + 8);
                    const u32 entry_type = load<u32>(entry + 16);

                    if (base > static_cast<u64>(-1) - length)
                        return false;

                    parsed.memory.add(base, length,
                                      static_cast<mm::memory_type>(entry_type));
                }

                saw_mmap = count != 0;

                break;
            }
            case tag_framebuffer: {
                if (size < 32)
                    return false;

                const u8* data = bytes + offset + 8;

                const u64 address = load<u64>(data);
                const u32 pitch   = load<u32>(data + 8);
                const u32 width   = load<u32>(data + 12);
                const u32 height  = load<u32>(data + 16);
                const u8  bpp     = data[20];
                const u8  type    = data[21];

                if (type == 1 && (bpp == 24 || bpp == 32) && size >= 38) {
                    const u8 red_position   = data[24];
                    const u8 red_size       = data[25];
                    const u8 green_position = data[26];
                    const u8 green_size     = data[27];
                    const u8 blue_position  = data[28];
                    const u8 blue_size      = data[29];

                    if (red_size == 8 && green_size == 8 && blue_size == 8 &&
                        ((red_position == 0 && green_position == 8 &&
                          blue_position == 16) ||
                         (red_position == 16 && green_position == 8 &&
                          blue_position == 0)))
                        parsed.framebuffer = {
                            address,
                            width,
                            height,
                            pitch,
                            bpp == 24
                                ? (red_position == 0 ? framebuffer_pixel_format::rgb888
                                                     : framebuffer_pixel_format::bgr888)
                                : (red_position == 0
                                       ? framebuffer_pixel_format::rgbx8888
                                       : framebuffer_pixel_format::bgrx8888),
                        };
                }

                break;
            }
            case tag_acpi_old:
                if (size < 8 + 20)
                    return false;

                if (preferred_rsdp == nullptr)
                    preferred_rsdp = bytes + offset + 8;

                break;
            case tag_acpi_new:
                if (size < 8 + 36)
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

        if (preferred_rsdp != nullptr)
            parsed.rsdp = reinterpret_cast<u64>(preferred_rsdp);

        out = parsed;

        return true;
    }
} // namespace astia::boot::virt
