#include "io.hpp"
#include "pic.hpp"
#include "smp.hpp"

#include <astia/arch/interrupts.hpp>
#include <astia/arch/platform.hpp>
#include <astia/dev/uart_platform.hpp>

namespace astia::arch::x86_64 {

    dev::pio_bus device_pio{
        nullptr, pio_read8, pio_read16, pio_read32,
        pio_write8, pio_write16, pio_write32};

} // namespace astia::arch::x86_64

namespace astia::arch::platform {

    u32 discovered_cpu_count() {
        return x86_64::smp::discovered_cpus();
    }

    u32 online_cpu_count() {
        return x86_64::smp::online_cpus();
    }

} // namespace astia::arch::platform

namespace astia::dev {

    u8 uart16550_platform_read8(void*, u16 port) {
        return arch::x86_64::inb(port);
    }

    u16 uart16550_platform_read16(void*, u16 port) {
        return arch::x86_64::inw(port);
    }

    u32 uart16550_platform_read32(void*, u16 port) {
        return arch::x86_64::inl(port);
    }

    void uart16550_platform_write8(void*, u16 port, u8 value) {
        arch::x86_64::outb(port, value);
    }

    void uart16550_platform_write16(void*, u16 port, u16 value) {
        arch::x86_64::outw(port, value);
    }

    void uart16550_platform_write32(void*, u16 port, u32 value) {
        arch::x86_64::outl(port, value);
    }

    bool uart16550_platform_claim(void*, irq_handler handler) {
        return arch::register_irq_handler(4, handler);
    }

    bool uart16550_platform_unmask(void*) {
        arch::x86_64::pic::unmask(4);
        return true;
    }

} // namespace astia::dev
