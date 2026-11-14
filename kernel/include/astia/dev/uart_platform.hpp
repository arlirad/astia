#pragma once

#include <astia/dev/capabilities.hpp>

namespace astia::dev {

    u8  uart16550_platform_read8(void* context, u16 port);
    u16 uart16550_platform_read16(void* context, u16 port);
    u32 uart16550_platform_read32(void* context, u16 port);
    void uart16550_platform_write8(void* context, u16 port, u8 value);
    void uart16550_platform_write16(void* context, u16 port, u16 value);
    void uart16550_platform_write32(void* context, u16 port, u32 value);
    bool uart16550_platform_claim(void* context, irq_handler handler);
    bool uart16550_platform_unmask(void* context);

    inline constexpr u16 uart16550_base = 0x3F8;

} // namespace astia::dev
