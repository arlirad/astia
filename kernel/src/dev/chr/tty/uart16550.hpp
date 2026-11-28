#pragma once

#include <astia/api/termios.h>
#include <astia/common.hpp>
#include <astia/dev/regs.hpp>
#include <astia/dev/uart_platform.hpp>

#include "../../console.hpp"
#include "../tty.hpp"

// COM1 as a 16550 UART, shaped by the dev layer's blk/char/net split:
// a uart16550 is a tty (line discipline: termios, TCGETS/TCSETS, a
// divisor-backed speed table), which is a chrdev, which is a device —
// the ring engine binds it through the generic device adapters and
// the vtable picks the port.
//
// Two faces of the same port:
//   - polled_putc(): the polled console primitive, byte at a time.
//     It works from the first instruction of kmain_entry — before any init
//     step, with the interrupt gate closed — because it touches
//     nothing but THR/LSR.  Panic dumps and early boot logs live here.
//   - The ring face: fds STDIN/STDOUT/STDERR are bound to the port by
//     the uart16550_irq init step, so an OP_WRITE drains the UART and an
//     OP_READ parks until the receive IRQ completes it.  That is the
//     path userspace will hit through SYS_IO_SUBMIT.
//
// The whole object is constant-initialized — constexpr constructor,
// vptr included — because this kernel runs no global constructors:
// the polled face must work from the first instruction, and it reads
// its port base straight out of the image.
//
// The class is the unit of extension: COM2..COM4 are one instance +
// one init_irq away (base/irq pair), everything else is per-port
// state already.

namespace astia::fs {
    class file;
}

namespace astia::dev {
    /// One 16550 UART as a terminal device.  Static storage only —
    /// constant-initialized and never destroyed, hence no virtual
    /// destructor.
    class uart16550 : public tty, public console_backend {
        public:
        constexpr uart16550(const char* name, pio_bus& pio, irq_line& irq, u16 base)
            : tty(name, dv_char | dv_sync), _ports(pio, base), _irq(irq) {}

        /// Polled console face: bounded THRE wait, then THR write;
        /// drops the byte on a dead port rather than hanging.
        void polled_putc(char c);
        bool write_byte(u8 byte) override;

        /// Discard pending input (selftests, line resets).
        void drain_rx();

        /// Bring the UART up: FIFOs on (1-byte receive trigger), 8N1,
        /// divisor 1 (115200 at the standard 1.8432 MHz clock), OUT2 so
        /// the PIC can see the line.  Interrupts stay off — the early
        /// phase runs before the PIC exists.
        bool init_uart();

        /// Arm the receive path: register the IRQ handler, bind the
        /// port to the ring's console fds, enable the data-ready
        /// interrupt and unmask the PIC line.
        bool init_irq();

        /// IRQ body: drain the receive FIFO into the queue, then nudge
        /// the ring engine on all three console fds.
        void irq();

        /// Character loopback (MCR bit 4): the port talks to itself.
        bool loopback(bool on);

        protected:
        /// The line runs at the divisor table's rates only; a TCSETS
        /// naming anything else is refused (-EINVAL).
        long tty_speed(speed_t speed) override;
        bool tty_putc(u8 byte) override;
        bool tty_output_ready() const override;
        void tty_ready() override;
        long tty_discard_input() override;

        private:
        bool tx_byte(u8 c);
        void rx_drain();

        io_port        _ports;
        irq_line&      _irq;
        core::spinlock _rx_lock;
        fs::file*      _ready_file = nullptr;
    };

    /// Bring COM1 up: FIFOs on, 8N1, divisor 1, OUT2, interrupts off.
    /// Called by the `uart16550` init step.
    bool uart16550_init();

    /// Arm the receive path on COM1: IRQ4 handler, ring fds, IER,
    /// PIC unmask.  Needs both `uart16550` (UART up) and `interrupts`
    /// (PIC remapped, IDT loaded).  Called by the `uart16550_irq` init
    /// step.
    bool uart16550_init_irq();

    /// Boot selftest: drive the ring end to end in UART loopback mode —
    /// a parked OP_READ completed from IRQ4 context, an OP_WRITE
    /// through the dispatch path, and every written byte read back.
    /// The caller owns the interrupt gate (kmain opens it, waits, then
    /// parks again); returns false on any mismatch.
    bool uart16550_ring_selftest();

    /// Polled final marker for boot harnesses; does not depend on the async
    /// descriptor/ring path being drainable.
    void uart16550_console_puts(const char* text);
} // namespace astia::dev
