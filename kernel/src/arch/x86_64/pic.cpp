#include "pic.hpp"

#include "io.hpp"

// The 8259 pair at its classic ports; the init sequence (ICW1..4) moves
// the IRQ vectors out of the CPU-exception range. QEMU starts with the
// PICs masked, and remap() keeps them that way — the tick source unmasks
// its own line (see interrupts.cpp).

namespace astia::arch::x86_64 {

    namespace {

        constexpr u16 master_cmd  = 0x20;
        constexpr u16 master_data = 0x21;
        constexpr u16 slave_cmd   = 0xA0;
        constexpr u16 slave_data  = 0xA1;

    } // namespace

    auto pic::remap() -> void {
        outb(master_cmd, 0x11); // ICW1: init + ICW4 needed
        outb(slave_cmd, 0x11);
        outb(master_data, irq_base);    // ICW2: master vector base
        outb(slave_data, irq_base + 8); // ICW2: slave vector base
        outb(master_data, 0x04);        // ICW3: slave hangs off IRQ2
        outb(slave_data, 0x02);         // ICW3: cascade identity
        outb(master_data, 0x01);        // ICW4: 8086 mode
        outb(slave_data, 0x01);
        outb(master_data, 0xFF); // OCW1: all IRQs masked
        outb(slave_data, 0xFF);  // OCW1: all slave IRQs masked
    }

    auto pic::unmask(u8 irq) -> void {
        const u16 port = irq < 8 ? master_data : slave_data;
        const u8  bit  = static_cast<u8>(1u << (irq & 7));
        outb(port, static_cast<u8>(inb(port) & static_cast<u8>(~bit)));
    }

    auto pic::eoi(u64 vector) -> void {
        if (vector >= irq_base + 8) {
            outb(slave_cmd, 0x20);
        }
        outb(master_cmd, 0x20);
    }

} // namespace astia::arch::x86_64
