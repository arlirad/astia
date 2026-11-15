#pragma once

#include <astia/dev/capabilities.hpp>

namespace astia::dev {

    extern pio_bus uart16550_platform_pio;
    extern irq_line uart16550_platform_irq;

    inline constexpr u16 uart16550_base = 0x3F8;

} // namespace astia::dev
