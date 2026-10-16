#include <astia/dev/uart16550.hpp>
#include <astia/init.hpp>

namespace astia::dev {

    // UART bring-up first: the console must be solid before paging,
    // heap or interrupts exist.  The ring/IRQ half is a separate step
    // because it needs the PIC remap and IDT that `interrupts` arms.
    static auto step_uart16550(init::context&) -> bool {
        return uart16550_init();
    }

    static auto step_uart16550_irq(init::context&) -> bool {
        return uart16550_init_irq();
    }

} // namespace astia::dev

ASTIA_INIT_STEP(early, uart16550, astia::dev::step_uart16550)
ASTIA_INIT_STEP(devices, uart16550_irq, astia::dev::step_uart16550_irq, "uart16550",
                "interrupts", "fs")
