#include "pic.hpp"

#include "io.hpp"

// The 8259 pair at its classic ports; the init sequence (ICW1..4) moves
// the IRQ vectors out of the CPU-exception range. QEMU starts with the
// PICs masked, and remap() keeps them that way — the tick source unmasks
// its own line (see interrupts.cpp).

namespace astia::arch::x86_64 {

    namespace {

        constexpr u16 master_command_port = 0x20;
        constexpr u16 master_data_port    = 0x21;
        constexpr u16 slave_command_port  = 0xA0;
        constexpr u16 slave_data_port     = 0xA1;

        constexpr u8 slave_irq_offset      = 8;
        constexpr u8 icw1_init_with_icw4   = 0x11;
        constexpr u8 icw3_master_slave_irq = 0x04;
        constexpr u8 icw3_slave_cascade_id = 0x02;
        constexpr u8 icw4_8086_mode        = 0x01;
        constexpr u8 all_irqs_masked       = 0xFF;
        constexpr u8 end_of_interrupt      = 0x20;

    } // namespace

    void pic::remap() {
        outb(master_command_port, icw1_init_with_icw4); // ICW1: init + ICW4 needed
        outb(slave_command_port, icw1_init_with_icw4);
        outb(master_data_port, irq_base);                   // ICW2: master vector base
        outb(slave_data_port, irq_base + slave_irq_offset); // ICW2: slave vector base
        outb(master_data_port, icw3_master_slave_irq);      // ICW3: slave hangs off IRQ2
        outb(slave_data_port, icw3_slave_cascade_id);       // ICW3: cascade identity
        outb(master_data_port, icw4_8086_mode);             // ICW4: 8086 mode
        outb(slave_data_port, icw4_8086_mode);
        outb(master_data_port, all_irqs_masked); // OCW1: all IRQs masked
        outb(slave_data_port, all_irqs_masked);  // OCW1: all slave IRQs masked
    }

    void pic::unmask(u8 irq) {
        const u16 port = irq < slave_irq_offset ? master_data_port : slave_data_port;
        const u8  bit  = static_cast<u8>(1u << (irq & 7));
        outb(port, static_cast<u8>(inb(port) & static_cast<u8>(~bit)));
    }

    void pic::eoi(u64 vector) {
        if (vector >= irq_base + slave_irq_offset) {
            outb(slave_command_port, end_of_interrupt);
        }
        outb(master_command_port, end_of_interrupt);
    }

} // namespace astia::arch::x86_64
