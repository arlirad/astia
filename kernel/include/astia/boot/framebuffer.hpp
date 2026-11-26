#pragma once

#include <astia/common.hpp>

namespace astia::boot {
    enum class framebuffer_pixel_format : u8 {
        rgb888,
        bgr888,
        rgbx8888,
        bgrx8888,
    };

    struct framebuffer_info {
        u64                      physical_address = 0;
        u32                      width            = 0;
        u32                      height           = 0;
        u32                      pitch            = 0;
        framebuffer_pixel_format pixel_format     = framebuffer_pixel_format::rgbx8888;

        bool valid() const {
            return physical_address != 0 && width != 0 && height != 0 && pitch != 0;
        }
    };
} // namespace astia::boot
