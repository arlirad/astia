#pragma once

#include <astia/common.hpp>

namespace astia::arch {

    /// Architecture-owned port I/O operations used by the public ports<T>
    /// wrapper.  An architecture provides the definitions; generic drivers
    /// can provide another backend as the ports<T, Backend> template argument.
    struct pio {
        static u8  read8(u16 port);
        static u16 read16(u16 port);
        static u32 read32(u16 port);
        static void write8(u16 port, u8 value);
        static void write16(u16 port, u16 value);
        static void write32(u16 port, u32 value);
    };

} // namespace astia::arch
