#include "debug_exit.hpp"

namespace astia::dev {

    namespace {
        constexpr u16 qemu_debug_exit_port = 0x501;
    }

    void debug_exit(u32 status) {
        // QEMU isa-debug-exit sits at this I/O port by default. The port has
        // to be in a 16-bit register (DX), hence the u16 cast.
        for (u32 attempt = 0; attempt < 8; ++attempt) {
            asm volatile("outb %0, %1"
                         :
                         : "a"(static_cast<u8>(status)), "d"(qemu_debug_exit_port)
                         : "memory");
        }
    }

} // namespace astia::dev
