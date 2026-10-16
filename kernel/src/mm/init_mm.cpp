#include "frame_allocator.hpp"
#include <astia/boot.hpp>
#include <astia/init.hpp>

namespace astia::mm {

    static bool step_frames(init::context& ctx) {
        if (ctx.info == nullptr) {
            return false;
        }
        kernel_frames().init(ctx.info->memory);
        return true;
    }

} // namespace astia::mm

ASTIA_INIT_STEP(memory, frames, astia::mm::step_frames, "boot_info")
