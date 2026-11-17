#include <astia/boot/virt/multiboot2.hpp>
#include <astia/boot/virt/pvh.hpp>
#include <astia/boot.hpp>
#include <astia/init.hpp>
#include <astia/log.hpp>

namespace astia::boot::virt {

    namespace {
        constexpr u64 identity_map_limit = 1ull << 30;

        bool identity_span(const void* address, span<const u8>& result) {
            const u64 physical = reinterpret_cast<u64>(address);
            if (physical >= identity_map_limit)
                return false;
            result = span<const u8>(static_cast<const u8*>(address),
                                    static_cast<usize>(identity_map_limit - physical));
            return true;
        }
    } // namespace

    static bool step_boot(init::context& ctx) {
        static astia::boot::boot_info info{};
        span<const u8> start_info;
        if (!identity_span(ctx.start_info, start_info))
            return false;

        const span<const u8> physical_memory =
            span<const u8>::bytes(0, static_cast<usize>(identity_map_limit));
        const span<const u8> pvh_start(start_info.data(), 56);
        if (!parse_start_info(pvh_start, physical_memory, info) &&
            !parse_multiboot2_info(start_info, info)) {
            return false;
        }
        if (info.framebuffer.valid()) {
            astia::log::info("boot framebuffer: paddr=%llx %ux%u pitch=%u format=%u",
                             info.framebuffer.physical_address, info.framebuffer.width,
                             info.framebuffer.height, info.framebuffer.pitch,
                             static_cast<u32>(info.framebuffer.pixel_format));
        }
        else {
            astia::log::info("boot framebuffer: absent");
        }
        ctx.info = &info;
        return true;
    }

} // namespace astia::boot::virt

ASTIA_INIT_STEP(early, boot_info, astia::boot::virt::step_boot)
