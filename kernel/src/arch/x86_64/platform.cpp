#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/arch/platform.hpp>
#include <astia/dev/uart_platform.hpp>

#include "io.hpp"
#include "pic.hpp"
#include "smp.hpp"

namespace {
    constinit astia::dev::pio_bus input_pio{nullptr,
                                            astia::arch::x86_64::pio_read8,
                                            astia::arch::x86_64::pio_read16,
                                            astia::arch::x86_64::pio_read32,
                                            astia::arch::x86_64::pio_write8,
                                            astia::arch::x86_64::pio_write16,
                                            astia::arch::x86_64::pio_write32};
}

namespace astia::dev {
    constinit pio_bus uart16550_platform_pio{nullptr,
                                             arch::x86_64::pio_read8,
                                             arch::x86_64::pio_read16,
                                             arch::x86_64::pio_read32,
                                             arch::x86_64::pio_write8,
                                             arch::x86_64::pio_write16,
                                             arch::x86_64::pio_write32};

    // Platform resource policy: the legacy byte-PIO ranges this platform is
    // willing to grant to a module. They describe what may be delegated, not
    // which device uses them.
    constexpr io_window_grant granted_io_windows[] = {
        {0x60, 0x64},
    };

    io_port_window claim_io_window(u16 first, u16 last) {
        constexpr usz grant_count =
            sizeof(granted_io_windows) / sizeof(granted_io_windows[0]);

        if (!io_window_is_granted(
                first, last,
                span<const io_window_grant>(granted_io_windows, grant_count)))
            return {};

        return io_port_window{input_pio, first, last};
    }
} // namespace astia::dev

namespace astia::arch::platform {
    void debug_exit(u32 status) {
        // QEMU isa-debug-exit sits at this I/O port by default.  Writing it is
        // a no-op when the device is not attached.
        constexpr u16 port = 0x501;

        for (u32 attempt = 0; attempt < 8; ++attempt) {
            x86_64::pio_write8(nullptr, port, static_cast<u8>(status));
        }
    }
} // namespace astia::arch::platform

namespace astia::arch::x86_64 {
    bool uart_irq_claim(void*, dev::irq_handler handler) {
        return register_irq_handler(4, handler);
    }

    bool uart_irq_unmask(void*) {
        pic::unmask(4);

        return true;
    }
}

namespace astia::dev {
    constinit irq_line uart16550_platform_irq{nullptr, arch::x86_64::uart_irq_claim,
                                              arch::x86_64::uart_irq_unmask};
}

namespace astia::arch::x86_64 {
    dev::pio_bus&  device_pio = dev::uart16550_platform_pio;
    dev::irq_line& uart_irq   = dev::uart16550_platform_irq;
} // namespace astia::arch::x86_64

namespace astia::arch::platform {
    u32 online_cpu_count() {
        return x86_64::smp::online_cpus();
    }
} // namespace astia::arch::platform
