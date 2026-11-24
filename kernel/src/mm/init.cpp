#include <astia/boot.hpp>
#include <astia/init.hpp>
#include <astia/mm/frame_allocator.hpp>

namespace astia::mm {

    static bool step_frames(init::context& ctx) {
        if (!ctx.info) {
            return false;
        }
        kernel_frames().init(ctx.info->memory);
        if (ctx.info->framebuffer.valid() && ctx.info->framebuffer.pitch != 0 &&
            static_cast<u64>(ctx.info->framebuffer.pitch) *
                    ctx.info->framebuffer.height <=
                ~u64{0} - ctx.info->framebuffer.physical_address) {
            kernel_frames().reserve(ctx.info->framebuffer.physical_address,
                                    static_cast<u64>(ctx.info->framebuffer.pitch) *
                                        ctx.info->framebuffer.height);
        }
        kernel_frames().reserve(ctx.info->initrd.paddr, ctx.info->initrd.size);
        for (usize i = 0; i < ctx.info->module_count; ++i)
            kernel_frames().reserve(ctx.info->modules[i].paddr,
                                    ctx.info->modules[i].size);
        return true;
    }

} // namespace astia::mm

ASTIA_INIT_STEP(memory, frames, astia::mm::step_frames, "boot_info")
