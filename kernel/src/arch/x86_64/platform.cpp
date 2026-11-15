#include "io.hpp"
#include "pic.hpp"
#include "smp.hpp"

#include <astia/arch/interrupts.hpp>
#include <astia/arch/platform.hpp>
#include <astia/dev/uart_platform.hpp>

namespace astia::dev {

    constinit pio_bus uart16550_platform_pio{
        nullptr, astia::arch::x86_64::pio_read8, astia::arch::x86_64::pio_read16,
        astia::arch::x86_64::pio_read32, astia::arch::x86_64::pio_write8,
        astia::arch::x86_64::pio_write16, astia::arch::x86_64::pio_write32};

}

namespace astia::arch::x86_64 {

    bool uart_irq_claim(void*, dev::irq_handler handler) {
        return arch::register_irq_handler(4, handler);
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

    dev::pio_bus& device_pio = dev::uart16550_platform_pio;
    dev::irq_line& uart_irq = dev::uart16550_platform_irq;

} // namespace astia::arch::x86_64

namespace astia::arch::platform {

    u32 discovered_cpu_count() {
        return x86_64::smp::discovered_cpus();
    }

    u32 online_cpu_count() {
        return x86_64::smp::online_cpus();
    }

} // namespace astia::arch::platform
