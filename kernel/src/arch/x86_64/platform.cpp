#include <astia/arch/interrupts.hpp>
#include <astia/arch/platform.hpp>
#include <astia/dev/input_platform.hpp>
#include <astia/dev/uart_platform.hpp>
#include <astia/module.hpp>

#include "io.hpp"
#include "pic.hpp"
#include "smp.hpp"

namespace astia::dev {
    constinit pio_bus               uart16550_platform_pio{nullptr,
                                             arch::x86_64::pio_read8,
                                             arch::x86_64::pio_read16,
                                             arch::x86_64::pio_read32,
                                             arch::x86_64::pio_write8,
                                             arch::x86_64::pio_write16,
                                             arch::x86_64::pio_write32};
    constinit pio_bus               ps2_platform_pio{nullptr,
                                       arch::x86_64::pio_read8,
                                       arch::x86_64::pio_read16,
                                       arch::x86_64::pio_read32,
                                       arch::x86_64::pio_write8,
                                       arch::x86_64::pio_write16,
                                       arch::x86_64::pio_write32};
    constinit const ps2_port_window ps2_platform_ports{ps2_platform_pio};
}

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

    bool ps2_irq_claim(void*, dev::irq_handler handler) {
        return register_irq_handler(1, handler);
    }

    bool ps2_irq_unmask(void*) {
        pic::unmask(1);

        return true;
    }

    bool ps2_irq_enter(void* context) {
        return static_cast<astia::module::lifetime_token*>(context)->try_enter();
    }

    void ps2_irq_leave(void* context) {
        static_cast<astia::module::lifetime_token*>(context)->leave();
    }
}

namespace astia::dev {
    constinit irq_line uart16550_platform_irq{nullptr, arch::x86_64::uart_irq_claim,
                                              arch::x86_64::uart_irq_unmask};
    constinit irq_line ps2_platform_irq{nullptr, arch::x86_64::ps2_irq_claim,
                                        arch::x86_64::ps2_irq_unmask};
}

namespace astia::module {
    const dev::ps2_port_window& ps2_platform_ports() {
        return dev::ps2_platform_ports;
    }

    bool ps2_claim_irq(arch::irq_handler handler, lifetime_token& token) {
        if (!token.valid())
            return false;

        return arch::register_irq_handler(1, handler, arch::x86_64::ps2_irq_enter,
                                          arch::x86_64::ps2_irq_leave, &token);
    }

    bool ps2_unmask_irq() {
        return dev::ps2_platform_irq.unmask();
    }
} // namespace astia::module

namespace astia::arch::x86_64 {
    dev::pio_bus&  device_pio = dev::uart16550_platform_pio;
    dev::irq_line& uart_irq   = dev::uart16550_platform_irq;
} // namespace astia::arch::x86_64

namespace astia::arch::platform {
    u32 online_cpu_count() {
        return x86_64::smp::online_cpus();
    }
} // namespace astia::arch::platform
