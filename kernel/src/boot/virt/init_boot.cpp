#include "pvh.hpp"
#include "multiboot2.hpp"
#include <astia/boot.hpp>
#include <astia/init.hpp>

namespace astia::boot::virt {

    static bool step_boot(init::context& ctx) {
        static astia::boot::boot_info info{};
        if (!parse_start_info(ctx.start_info, info) &&
            !parse_multiboot2_info(ctx.start_info, info)) {
            return false;
        }
        ctx.info = &info;
        return true;
    }

} // namespace astia::boot::virt

ASTIA_INIT_STEP(early, boot_info, astia::boot::virt::step_boot)
