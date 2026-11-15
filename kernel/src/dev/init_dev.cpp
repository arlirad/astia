#include "chr/pseudo.hpp"
#include "chr/tty/uart16550.hpp"
#include "chr/tty/virtual_tty.hpp"
#include "pci.hpp"
#include <astia/init.hpp>
#include <astia/log.hpp>

namespace astia::dev {

    // UART bring-up first: the console must be solid before paging,
    // heap or interrupts exist.  The ring/IRQ half is a separate step
    // because it needs the PIC remap and IDT that `interrupts` arms.
    static bool step_uart16550(init::context&) {
        return uart16550_init();
    }

    static bool step_uart16550_irq(init::context&) {
        return uart16550_init_irq();
    }

    static bool step_virtual_tty(init::context&) {
        return virtual_tty_init();
    }

    static bool step_pseudo(init::context&) {
        return pseudo_devices_init();
    }

    static bool step_pci(init::context&) {
        const u32 found = pci_enumerate();
        astia::log::info("pci: enumerated %u function(s)", found);
        return true;
    }

} // namespace astia::dev

ASTIA_INIT_STEP(early, uart16550, astia::dev::step_uart16550)
ASTIA_INIT_STEP(devices, uart16550_irq, astia::dev::step_uart16550_irq, "uart16550",
                "interrupts", "fs")
ASTIA_INIT_STEP(devices, virtual_tty, astia::dev::step_virtual_tty, "uart16550_irq", "fs")
ASTIA_INIT_STEP(devices, pseudo, astia::dev::step_pseudo, "fs")
ASTIA_INIT_STEP(devices, pci, astia::dev::step_pci, "uart16550_irq", "mod")
