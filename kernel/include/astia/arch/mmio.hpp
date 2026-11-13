#pragma once

#include <astia/common.hpp>

namespace astia::arch {

    struct mmio_window_bounds {
        vaddr_t base;
        vaddr_t limit;
    };

    mmio_window_bounds mmio_window();

} // namespace astia::arch
