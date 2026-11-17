#include <astia/boot/virt/multiboot2.hpp>
#include <astia/boot/virt/pvh.hpp>
#include <astia/boot.hpp>
#include <astia/init.hpp>
#include <astia/log.hpp>

namespace astia::boot::virt {

    static bool step_boot(init::context& ctx) {
        static astia::boot::boot_info info{};
        if (!parse_start_info(ctx.start_info, info) &&
            !parse_multiboot2_info(ctx.start_info, info)) {
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
