#include "uart16550.hpp"

#include "../../device_file.hpp"
#include "aio.hpp"
#include "pic.hpp"
#include "proc.hpp"
#include <astia/api/aio.h>
#include <astia/api/errno.h>
#include <astia/api/syscalls.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/interrupts.hpp>
#include <astia/memory.hpp>

/*
 * COM1 (the classic 0x3F8 port pair), driven two ways — as a device
 * in the blk/char/net split: a uart16550 is a tty, so the line
 * discipline (termios, TCGETS/TCSETS, the divisor speed table) lives
 * in the tty layer, the byte moving in the driver, and the ring
 * engine reaches the port through the generic device adapters.
 *
 * Polled face: polled_putc() spins on LSR.THRE and writes THR.
 * Bounded — a wedged UART drops the byte instead of hanging the
 * caller, which matters because every early-boot log line and every
 * fatal exception dump goes through here before any init step has run.
 *
 * Async face: the receive FIFO drains into the per-port byte queue
 * from the IRQ4 handler, which then nudges the ring engine
 * (sys::io_device_ready).  A parked OP_READ wakes with the bytes that
 * arrived; OP_WRITE drains the UART through the dispatch path.  The
 * boot selftest closes the loop in UART loopback mode: what it writes
 * through the ring comes back through the receive interrupt, so the
 * submit -> park -> IRQ -> complete cycle is proven on every boot
 * without any external uart16550 input.
 *
 * Locking: the byte queue is single-producer (the IRQ) and
 * single-consumer (whichever face pops — guarded by the interrupt gate
 * because both ring dispatch and the IRQ's own retry may consume).
 */

namespace astia::dev {

    namespace {

        // 16550 register offsets from the port base.
        constexpr u32 reg_data = 0; // RBR (r) / THR (w) / DLL (DLAB on)
        constexpr u32 reg_ier  = 1; // IER / DLM (DLAB on)
        constexpr u32 reg_fcr  = 2; // FCR (w)
        constexpr u32 reg_lcr  = 3;
        constexpr u32 reg_mcr  = 4;
        constexpr u32 reg_lsr  = 5;

        constexpr u8 lsr_data_ready = 0x01;
        constexpr u8 lsr_thre       = 0x20;

        constexpr u8 ier_rx_ready = 0x01; // ERBFI: data-ready interrupt

        constexpr u8 fcr_enable = 0x07; // FIFOs on, both cleared, 1-byte trigger
        constexpr u8 lcr_dlab   = 0x80;
        constexpr u8 lcr_8n1    = 0x03;

        constexpr u8   mcr_dtr       = 0x01;
        constexpr u8   mcr_rts       = 0x02;
        constexpr u8   mcr_out2      = 0x08; // gates the IRQ line onto the PIC
        constexpr u8   mcr_loopback  = 0x10;
        constexpr char device_path[] = "/dev/uart16550";

        // QEMU answers THR instantly and real hardware shifts a byte in
        // under 100 µs at 115200; the cap fires only on a dead port,
        // where losing a console byte beats wedging the boot.
        constexpr u32 thre_spins = 1u << 22;

        // Constant-initialized, mutable, and placed in writable data:
        // the polled console works before (and without) any
        // constructor having run, which is what a kernel without
        // .init_array needs.  constinit enforces static initialization
        // while retaining writable RX state and cursors.
        constinit uart16550   com1{"ttyS0", 0x3F8, 4};
        constinit device_file com1_file{&com1};

        void uart16550_irq() {
            com1.irq();
        }

        void console_putc(char c) {
            com1.polled_putc(c);
        }

        void console_puts(const char* s) {
            while (*s != '\0') {
                console_putc(*s++);
            }
        }

        // The selftest body.  Returns null on success, else a reason —
        // the caller reports it only after loopback is off, because
        // while the loop is closed every console byte goes back into
        // the receive FIFO instead of out the port.
        const char* ring_selftest_body(astia_rings* ring, char* got) {
            constexpr char payload[]   = "ping";
            constexpr u32  payload_len = sizeof(payload) - 1;

            // Open the device through the VFS first.  The rest of this
            // test deliberately uses the returned descriptor rather
            // than the standard descriptors installed at init time.
            astia_sqe* s = astia_sqe_slot(ring);
            if (s == nullptr) {
                return "sq full (open)";
            }
            *s           = astia_sqe{};
            s->op        = OP_OPENAT;
            s->fd        = -1; // current VFS has one exact-path namespace
            s->address   = reinterpret_cast<u64>(device_path);
            s->aux       = 0;
            s->user_data = 0xC0DE00;
            astia_sq_publish(ring, 1);
            if (sys::io_enter(ring, 1, 1, ENTER_GETEVENTS, 100) < 0) {
                return "open kick failed";
            }
            astia_cqe* open_cqe = astia_cq_peek(ring);
            if (open_cqe == nullptr || open_cqe->user_data != 0xC0DE00 ||
                open_cqe->result < 0) {
                return "open mismatch";
            }
            i32 fd = open_cqe->result;
            astia_cq_consume(ring, 1);

            // dup shares the open file description, then dup2 aliases
            // that same description at an explicitly selected handle.
            s = astia_sqe_slot(ring);
            if (s == nullptr) {
                return "sq full (dup)";
            }
            *s           = astia_sqe{};
            s->op        = OP_DUP;
            s->fd        = fd;
            s->user_data = 0xC0DE05;
            astia_sq_publish(ring, 1);
            if (sys::io_enter(ring, 1, 1, ENTER_GETEVENTS, 100) < 0) {
                return "dup kick failed";
            }
            astia_cqe* dup_cqe = astia_cq_peek(ring);
            if (dup_cqe == nullptr || dup_cqe->user_data != 0xC0DE05 ||
                dup_cqe->result < 0) {
                return "dup mismatch";
            }
            const i32 dup_fd = dup_cqe->result;
            astia_cq_consume(ring, 1);

            s = astia_sqe_slot(ring);
            if (s == nullptr) {
                return "sq full (dup2)";
            }
            *s           = astia_sqe{};
            s->op        = OP_DUP2;
            s->fd        = dup_fd;
            s->offset    = 5;
            s->user_data = 0xC0DE06;
            astia_sq_publish(ring, 1);
            if (sys::io_enter(ring, 1, 1, ENTER_GETEVENTS, 100) < 0) {
                return "dup2 kick failed";
            }
            astia_cqe* dup2_cqe = astia_cq_peek(ring);
            if (dup2_cqe == nullptr || dup2_cqe->user_data != 0xC0DE06 ||
                dup2_cqe->result != 5) {
                return "dup2 mismatch";
            }
            astia_cq_consume(ring, 1);

            // Close the original two aliases and continue using only
            // the dup2 target.  The shared open_file must survive both
            // closes and remain usable through descriptor 5.
            const i32 aliases[2] = {fd, dup_fd};
            for (u32 alias = 0; alias < 2; ++alias) {
                const i32 close_fd = aliases[alias];
                s                  = astia_sqe_slot(ring);
                if (s == nullptr) {
                    return "sq full (alias close)";
                }
                *s           = astia_sqe{};
                s->op        = OP_CLOSE;
                s->fd        = close_fd;
                s->user_data = 0xC0DE07 + static_cast<u64>(close_fd == dup_fd);
                astia_sq_publish(ring, 1);
                if (sys::io_enter(ring, 1, 1, ENTER_GETEVENTS, 100) < 0) {
                    return "alias close kick failed";
                }
                astia_cqe* c = astia_cq_peek(ring);
                if (c == nullptr || c->result != 0) {
                    return "alias close mismatch";
                }
                astia_cq_consume(ring, 1);
            }
            fd = 5;

            // As a userspace client would: fill SQEs through the ABI
            // helpers, publish, kick.  The READ goes in first so it is
            // guaranteed to park — no data exists yet — and the WRITE
            // is what frees it, from IRQ4, once the bytes loop back.
            s = astia_sqe_slot(ring);
            if (s == nullptr) {
                return "sq full";
            }
            *s           = astia_sqe{};
            s->op        = OP_READ;
            s->fd        = fd;
            s->address   = reinterpret_cast<u64>(got);
            s->length    = payload_len;
            s->user_data = 0xC0DE01;
            astia_sq_publish(ring, 1);

            s = astia_sqe_slot(ring);
            if (s == nullptr) {
                return "sq full (write)";
            }
            *s           = astia_sqe{};
            s->op        = OP_WRITE;
            s->fd        = fd;
            s->address   = reinterpret_cast<u64>(payload);
            s->length    = payload_len;
            s->user_data = 0xC0DE02;
            astia_sq_publish(ring, 1);

            // One kick submits both and waits for both completions; the
            // read's arrives from the receive interrupt, the write's
            // from the dispatch itself.  100 ticks is a generous second.
            const long rc = sys::io_enter(ring, 2, 2, ENTER_GETEVENTS, 100);
            if (rc < 0) {
                return "kick failed";
            }

            // Reap by tag: completion order is not an ABI promise.
            long read_count  = -1;
            long write_count = -1;
            while (astia_cqe* c = astia_cq_peek(ring)) {
                if (c->user_data == 0xC0DE01) {
                    read_count = c->result;
                }
                else if (c->user_data == 0xC0DE02) {
                    write_count = c->result;
                }
                astia_cq_consume(ring, 1);
            }
            if (write_count != static_cast<long>(payload_len) || read_count < 1) {
                return "first pass mismatch";
            }
            u32 have = static_cast<u32>(read_count);

            // The interrupt may complete the parked read on the first
            // looped-back byte, so the tail of the payload arrives
            // through follow-up reads — park, wake, repeat until the
            // string is whole.
            for (u32 round = 0; have < payload_len && round < 8; ++round) {
                s = astia_sqe_slot(ring);
                if (s == nullptr) {
                    return "sq full (collect)";
                }
                *s           = astia_sqe{};
                s->op        = OP_READ;
                s->fd        = fd;
                s->address   = reinterpret_cast<u64>(got + have);
                s->length    = payload_len - have;
                s->user_data = 0xC0DE03 + round;
                astia_sq_publish(ring, 1);

                const long r2 = sys::io_enter(ring, 1, 1, ENTER_GETEVENTS, 100);
                if (r2 < 0) {
                    return "collect kick failed";
                }
                astia_cqe* c = astia_cq_peek(ring);
                if (c == nullptr || c->user_data != 0xC0DE03 + round || c->result < 1) {
                    return "collect mismatch";
                }
                have += static_cast<u32>(c->result);
                astia_cq_consume(ring, 1);
            }

            if (have != payload_len) {
                return "short read";
            }
            if (memcmp(got, payload, payload_len) != 0) {
                return "payload mismatch";
            }

            // Close through the same ring path.  This also proves the
            // returned descriptor was a real VFS file, not just a
            // transient device-table alias.
            s = astia_sqe_slot(ring);
            if (s == nullptr) {
                return "sq full (close)";
            }
            *s           = astia_sqe{};
            s->op        = OP_CLOSE;
            s->fd        = fd;
            s->user_data = 0xC0DE04;
            astia_sq_publish(ring, 1);
            if (sys::io_enter(ring, 1, 1, ENTER_GETEVENTS, 100) < 0) {
                return "close kick failed";
            }
            astia_cqe* close_cqe = astia_cq_peek(ring);
            if (close_cqe == nullptr || close_cqe->user_data != 0xC0DE04 ||
                close_cqe->result != 0) {
                return "close mismatch";
            }
            astia_cq_consume(ring, 1);
            return nullptr;
        }

    } // namespace

    bool uart16550::tx_byte(u8 c) {
        for (u32 i = 0; i < thre_spins; ++i) {
            if ((_ports[reg_lsr] & lsr_thre) != 0) {
                _ports[reg_data] = c;
                return true;
            }
        }
        return false;
    }

    void uart16550::polled_putc(char c) {
        (void) tx_byte(static_cast<u8>(c));
    }

    void uart16550::rx_drain() {
        // The tty owns buffering; the UART only drains its hardware FIFO.
        while ((_ports[reg_lsr] & lsr_data_ready) != 0) {
            tty_receive(_ports[reg_data]);
        }
    }

    void uart16550::drain_rx() {
        arch::interrupt_guard gate;
        while ((_ports[reg_lsr] & lsr_data_ready) != 0) {
            (void) _ports[reg_data];
        }
        tty_discard_input();
    }

    // Read and write are implemented by tty; this class only owns the UART.

    void uart16550::irq() {
        rx_drain();
        tty_input_ready();
    }

    void uart16550::loopback(bool on) {
        const u8 mcr    = mcr_dtr | mcr_rts | mcr_out2;
        _ports[reg_mcr] = on ? static_cast<u8>(mcr | mcr_loopback) : mcr;
    }

    bool uart16550::tty_putc(u8 byte) {
        return tx_byte(byte);
    }

    void uart16550::tty_ready() {
        sys::io_device_ready_context(&com1_file);
    }

    long uart16550::tty_speed(speed_t speed) {
        // POSIX speed index -> 16550 divisor at the 1.8432 MHz clock
        // (baud = 115200 / divisor).  The line runs at exactly the
        // rates the table names; anything else is refused rather than
        // silently approximated.
        u16 divisor = 0;
        switch (speed) {
        case B0:
            divisor = 0; // line suspended
            break;
        case B300:
            divisor = 384;
            break;
        case B1200:
            divisor = 96;
            break;
        case B2400:
            divisor = 48;
            break;
        case B4800:
            divisor = 24;
            break;
        case B9600:
            divisor = 12;
            break;
        case B19200:
            divisor = 6;
            break;
        case B38400:
            divisor = 3;
            break;
        case B57600:
            divisor = 2;
            break;
        case B115200:
            divisor = 1;
            break;
        default:
            return -EINVAL;
        }
        const u8 lcr     = _ports[reg_lcr];
        _ports[reg_lcr]  = static_cast<u8>(lcr | lcr_dlab);
        _ports[reg_data] = static_cast<u8>(divisor & 0xFF);
        _ports[reg_ier]  = static_cast<u8>(divisor >> 8);
        _ports[reg_lcr]  = lcr;
        return 0;
    }

    void uart16550::init_uart() {
        _ports[reg_ier]  = 0x00;     // interrupts off during bring-up
        _ports[reg_lcr]  = lcr_dlab; // divisor latch on
        _ports[reg_data] = 0x01;     // divisor low byte: 1 -> 115200
        _ports[reg_ier]  = 0x00;     // divisor high byte (IER shares the latch)
        _ports[reg_lcr]  = lcr_8n1;  // 8N1, latch off
        _ports[reg_fcr]  = fcr_enable;
        _ports[reg_mcr]  = mcr_dtr | mcr_rts | mcr_out2;
    }

    bool uart16550::init_irq() {
        if (!arch::register_irq_handler(_irq, uart16550_irq)) {
            return false; // PIC line already taken
        }
        if (fs::register_device(device_path, mm::shptr<fs::file>::borrowed(&com1_file)) <
            0) {
            return false;
        }
        _ports[reg_ier] = ier_rx_ready;  // data-ready interrupts on
        arch::x86_64::pic::unmask(_irq); // handler in place first
        return true;
    }

    bool uart16550_init() {
        com1.init_uart();
        return true;
    }

    bool uart16550_init_irq() {
        return com1.init_irq();
    }

    bool uart16550_ring_selftest() {
        constexpr char payload[]   = "ping";
        constexpr u32  payload_len = sizeof(payload) - 1;

        console_puts("astia: uart16550 ring: start\n");

        astia_rings* ring = nullptr;
        if (sys::io_setup(8, 0, &ring) < 0 || ring == nullptr) {
            console_puts("astia: uart16550 ring: no ring\n");
            return false;
        }

        // Start from an empty line, then let the port talk to itself.
        com1.drain_rx();
        com1.loopback(true);

        char        got[payload_len] = {};
        const char* why              = ring_selftest_body(ring, got);

        // Close the loop before reporting: while it is open, console
        // output feeds the receive FIFO instead of leaving the port.
        com1.loopback(false);
        // The ring selftest consumes its own payload through a separate
        // descriptor, but the tty line discipline is shared with stdin.
        // Discard any residual loopback bytes before userspace inherits the
        // console, otherwise zsh starts with a phantom `ping` command.
        com1.drain_rx();
        if (why != nullptr) {
            console_puts("astia: uart16550 ring: ");
            console_puts(why);
            console_puts("\n");
        }
        (void) sys::io_destroy(ring);
        return why == nullptr;
    }

    void uart16550_console_puts(const char* text) {
        if (text == nullptr) {
            return;
        }
        while (*text != '\0') {
            com1.polled_putc(*text++);
        }
    }

} // namespace astia::dev
