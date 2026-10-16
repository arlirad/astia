#include "debug_exit.hpp"

namespace astia::dev {

    void debug_exit(u32 status) {
        // QEMU isa-debug-exit sits at I/O port 0x501 by default. The port has
        // to be in a 16-bit register (DX), hence the u16 cast.
        asm volatile("outb %0, %1"
                     :
                     : "a"(static_cast<u8>(status)), "d"(static_cast<u16>(0x501))
                     : "memory");
    }

} // namespace astia::dev
