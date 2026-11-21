#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>

#include "../src/sys/aio.hpp"
#include "chr/tty/uart16550.hpp"
#include "support/test.hpp"

#include <cstring>

namespace astia::arch {
    namespace {
        bool interrupts_open = true;
    }

    void cpu::interrupts() {
        interrupts_open = true;
    }
    void cpu::nointerrupts() {
        interrupts_open = false;
    }
    bool cpu::check_interrupts() {
        return interrupts_open;
    }
} // namespace astia::arch

namespace astia::dev {
    bool virtual_tty_forward_input(u8) {
        return false;
    }
} // namespace astia::dev

namespace astia::sys {
    void io_device_ready_context(const io_device*) {}
} // namespace astia::sys

namespace astia::dev {
    pio_bus  uart16550_platform_pio{};
    irq_line uart16550_platform_irq{};
} // namespace astia::dev

namespace {

    constexpr astia::u16 fake_base = 0x3F8;

    struct fake_uart {
        static constexpr astia::u32 rx_capacity = 32;

        astia::u8  ier        = 0;
        astia::u8  lcr        = 0;
        astia::u8  mcr        = 0;
        astia::u8  divisor_lo = 0;
        astia::u8  divisor_hi = 0;
        astia::u8  rx[rx_capacity]{};
        astia::u32 head = 0;
        astia::u32 tail = 0;

        void push(astia::u8 byte) {
            if (head - tail == rx_capacity)
                return;
            rx[head & (rx_capacity - 1)] = byte;
            ++head;
        }

        bool empty() const {
            return head == tail;
        }
    };

    astia::u8 fake_read8(void* context, astia::u16 port) {
        auto& fake = *static_cast<fake_uart*>(context);
        switch (port - fake_base) {
        case 0:
            if ((fake.lcr & 0x80) != 0)
                return fake.divisor_lo;
            if (fake.head == fake.tail)
                return 0;
            return fake.rx[(fake.tail++) & (fake_uart::rx_capacity - 1)];
        case 1:
            return (fake.lcr & 0x80) != 0 ? fake.divisor_hi : fake.ier;
        case 2:
            return 0;
        case 3:
            return fake.lcr;
        case 4:
            return fake.mcr;
        case 5:
            return static_cast<astia::u8>(0x60 | (fake.head == fake.tail ? 0 : 0x01));
        default:
            return 0xFF;
        }
    }

    astia::u16 fake_read16(void* context, astia::u16 port) {
        return static_cast<astia::u16>(
            fake_read8(context, port) |
            (fake_read8(context, static_cast<astia::u16>(port + 1)) << 8));
    }

    astia::u32 fake_read32(void* context, astia::u16 port) {
        return static_cast<astia::u32>(fake_read16(context, port)) |
               (static_cast<astia::u32>(
                    fake_read16(context, static_cast<astia::u16>(port + 2)))
                << 16);
    }

    void fake_write8(void* context, astia::u16 port, astia::u8 value) {
        auto& fake = *static_cast<fake_uart*>(context);
        switch (port - fake_base) {
        case 0:
            if ((fake.lcr & 0x80) != 0)
                fake.divisor_lo = value;
            else if ((fake.mcr & 0x10) != 0)
                fake.push(value);
            break;
        case 1:
            if ((fake.lcr & 0x80) != 0)
                fake.divisor_hi = value;
            else
                fake.ier = value;
            break;
        case 2:
            if ((value & 0x02) != 0)
                fake.head = fake.tail = 0;
            break;
        case 3:
            fake.lcr = value;
            break;
        case 4:
            fake.mcr = value;
            break;
        default:
            break;
        }
    }

    void fake_write16(void* context, astia::u16 port, astia::u16 value) {
        fake_write8(context, port, static_cast<astia::u8>(value));
        fake_write8(context, static_cast<astia::u16>(port + 1),
                    static_cast<astia::u8>(value >> 8));
    }

    void fake_write32(void* context, astia::u16 port, astia::u32 value) {
        fake_write16(context, port, static_cast<astia::u16>(value));
        fake_write16(context, static_cast<astia::u16>(port + 2),
                     static_cast<astia::u16>(value >> 16));
    }

    astia::dev::pio_bus fake_bus(fake_uart& fake) {
        return astia::dev::pio_bus{&fake,       fake_read8,   fake_read16, fake_read32,
                                   fake_write8, fake_write16, fake_write32};
    }

    void raw_mode(astia::dev::uart16550& uart) {
        struct termios value {};
        ASTIA_CHECK(uart.ioctl(TCGETS, &value, sizeof(value)) == 0);
        value.c_iflag     = 0;
        value.c_oflag     = 0;
        value.c_lflag     = 0;
        value.c_cc[VMIN]  = 1;
        value.c_cc[VTIME] = 0;
        ASTIA_CHECK(uart.ioctl(TCSETS, &value, sizeof(value)) == 0);
    }

} // namespace

ASTIA_TEST(uart16550_tcflsh_drains_receive_fifo_before_software_input) {
    fake_uart             fake;
    astia::dev::pio_bus   bus = fake_bus(fake);
    astia::dev::irq_line  irq;
    astia::dev::uart16550 uart("uart-test", bus, irq, fake_base);
    ASTIA_CHECK(uart.init_uart());
    raw_mode(uart);

    fake.push('a');
    fake.push('b');
    int selector = TCIFLUSH;
    ASTIA_CHECK(uart.ioctl(TCFLSH, &selector, sizeof(selector)) == 0);
    ASTIA_CHECK(fake.empty());

    char result[4]{};
    ASTIA_CHECK(uart.read(result, sizeof(result), 0) == 0);

    fake.push('z');
    uart.irq();
    ASTIA_CHECK(uart.read(result, sizeof(result), 0) == 1);
    ASTIA_CHECK(result[0] == 'z');
}

ASTIA_TEST(uart16550_tcflsh_discards_delivered_input_then_accepts_new_input) {
    fake_uart             fake;
    astia::dev::pio_bus   bus = fake_bus(fake);
    astia::dev::irq_line  irq;
    astia::dev::uart16550 uart("uart-test", bus, irq, fake_base);
    ASTIA_CHECK(uart.init_uart());
    raw_mode(uart);

    fake.push('x');
    uart.irq();
    fake.push('o');
    fake.push('l');
    fake.push('d');

    int selector = TCIFLUSH;
    ASTIA_CHECK(uart.ioctl(TCFLSH, &selector, sizeof(selector)) == 0);
    ASTIA_CHECK(fake.empty());
    char result[4]{};
    ASTIA_CHECK(uart.read(result, sizeof(result), 0) == 0);

    fake.push('n');
    fake.push('e');
    fake.push('w');
    uart.irq();
    ASTIA_CHECK(uart.read(result, sizeof(result), 0) == 3);
    ASTIA_CHECK(result[0] == 'n' && result[1] == 'e' && result[2] == 'w');
}

ASTIA_TEST(uart16550_tcsetsf_drains_receive_fifo) {
    fake_uart             fake;
    astia::dev::pio_bus   bus = fake_bus(fake);
    astia::dev::irq_line  irq;
    astia::dev::uart16550 uart("uart-test", bus, irq, fake_base);
    ASTIA_CHECK(uart.init_uart());
    raw_mode(uart);

    fake.push('a');
    fake.push('b');
    struct termios value {};
    ASTIA_CHECK(uart.ioctl(TCGETS, &value, sizeof(value)) == 0);
    ASTIA_CHECK(uart.ioctl(TCSETSF, &value, sizeof(value)) == 0);
    ASTIA_CHECK(fake.empty());
    char result[4]{};
    ASTIA_CHECK(uart.read(result, sizeof(result), 0) == 0);
}

ASTIA_TEST(uart16550_termios_round_trip_preserves_state) {
    fake_uart             fake;
    astia::dev::pio_bus   bus = fake_bus(fake);
    astia::dev::irq_line  irq;
    astia::dev::uart16550 uart("uart-test", bus, irq, fake_base);
    ASTIA_CHECK(uart.init_uart());

    struct termios next {};
    ASTIA_CHECK(uart.ioctl(TCGETS, &next, sizeof(next)) == 0);
    next.c_iflag      = ICRNL | IXON | IGNCR;
    next.c_oflag      = OPOST;
    next.c_cflag      = CS7 | CREAD | PARENB;
    next.c_lflag      = ISIG | ICANON | ECHO | ECHOE;
    next.c_cc[VERASE] = 8;
    next.c_cc[VKILL]  = 21;
    next.c_cc[VMIN]   = 4;
    next.c_cc[VTIME]  = 2;
    next.c_ispeed = next.c_ospeed = B9600;
    ASTIA_CHECK(uart.ioctl(TCSETS, &next, sizeof(next)) == 0);
    ASTIA_CHECK(fake.divisor_lo == 12);

    struct termios got {};
    ASTIA_CHECK(uart.ioctl(TCGETS, &got, sizeof(got)) == 0);
    ASTIA_CHECK(got.c_iflag == next.c_iflag && got.c_oflag == next.c_oflag &&
                got.c_cflag == next.c_cflag && got.c_lflag == next.c_lflag);
    ASTIA_CHECK(got.c_line == next.c_line && got.c_ispeed == next.c_ispeed &&
                got.c_ospeed == next.c_ospeed);
    for (astia::u32 i = 0; i < NCCS; ++i)
        ASTIA_CHECK(got.c_cc[i] == next.c_cc[i]);

    struct termios bad = next;
    bad.c_line         = 1;
    ASTIA_CHECK(uart.ioctl(TCSETS, &bad, sizeof(bad)) == -EINVAL);
    bad          = next;
    bad.c_ospeed = B115200;
    ASTIA_CHECK(uart.ioctl(TCSETS, &bad, sizeof(bad)) == -EINVAL);
    ASTIA_CHECK(uart.ioctl(TCGETS, &got, sizeof(got)) == 0);
    ASTIA_CHECK(got.c_line == next.c_line && got.c_ispeed == next.c_ispeed &&
                got.c_ospeed == next.c_ospeed);
}

ASTIA_TEST(uart16550_fifo_preserves_byte_order) {
    fake_uart             fake;
    astia::dev::pio_bus   bus = fake_bus(fake);
    astia::dev::irq_line  irq;
    astia::dev::uart16550 uart("uart-test", bus, irq, fake_base);
    ASTIA_CHECK(uart.init_uart());
    raw_mode(uart);

    astia::u8 payload[32]{};
    for (astia::u32 i = 0; i < sizeof(payload); ++i) {
        payload[i] = static_cast<astia::u8>(i * 7 + 3);
        fake.push(payload[i]);
    }
    ASTIA_CHECK(fake.head - fake.tail == sizeof(payload));
    uart.irq();
    ASTIA_CHECK(fake.empty());

    char result[sizeof(payload)]{};
    ASTIA_CHECK(uart.read(result, sizeof(result), 0) ==
                static_cast<long>(sizeof(result)));
    ASTIA_CHECK(memcmp(result, payload, sizeof(payload)) == 0);
}

ASTIA_TEST(uart16550_overflow_drops_excess_and_recovers_after_flush) {
    fake_uart             fake;
    astia::dev::pio_bus   bus = fake_bus(fake);
    astia::dev::irq_line  irq;
    astia::dev::uart16550 uart("uart-test", bus, irq, fake_base);
    ASTIA_CHECK(uart.init_uart());
    raw_mode(uart);

    for (astia::u32 round = 0; round < 40; ++round) {
        for (astia::u32 i = 0; i < 32; ++i)
            fake.push(static_cast<astia::u8>(round * 32 + i));
        uart.irq();
    }
    char result[2048]{};
    long count = uart.read(result, sizeof(result), 0);
    ASTIA_CHECK(count == 1023);
    for (long i = 0; i < count; ++i)
        ASTIA_CHECK(static_cast<astia::u8>(result[i]) ==
                    static_cast<astia::u8>(i & 0xFF));

    for (astia::u32 round = 0; round < 40; ++round) {
        for (astia::u32 i = 0; i < 32; ++i)
            fake.push(static_cast<astia::u8>(round * 32 + i));
        uart.irq();
    }
    int selector = TCIFLUSH;
    ASTIA_CHECK(uart.ioctl(TCFLSH, &selector, sizeof(selector)) == 0);
    ASTIA_CHECK(uart.read(result, sizeof(result), 0) == 0);

    fake.push('o');
    fake.push('k');
    uart.irq();
    ASTIA_CHECK(uart.read(result, sizeof(result), 0) == 2);
    ASTIA_CHECK(result[0] == 'o' && result[1] == 'k');
}

ASTIA_TEST(uart16550_rejects_invalid_and_unsupported_ioctl_requests) {
    fake_uart             fake;
    astia::dev::pio_bus   bus = fake_bus(fake);
    astia::dev::irq_line  irq;
    astia::dev::uart16550 uart("uart-test", bus, irq, fake_base);
    ASTIA_CHECK(uart.init_uart());

    int selector = TCIFLUSH;
    ASTIA_CHECK(uart.ioctl(TCFLSH, nullptr, sizeof(selector)) == -EINVAL);
    ASTIA_CHECK(uart.ioctl(TCFLSH, &selector, sizeof(selector) - 1) == -EINVAL);
    selector = 99;
    ASTIA_CHECK(uart.ioctl(TCFLSH, &selector, sizeof(selector)) == -EINVAL);
    selector = TCOFLUSH;
    ASTIA_CHECK(uart.ioctl(TCFLSH, &selector, sizeof(selector)) == -ENOSYS);
    selector = TCIOFLUSH;
    ASTIA_CHECK(uart.ioctl(TCFLSH, &selector, sizeof(selector)) == -ENOSYS);
    ASTIA_CHECK(uart.ioctl(0x1234, nullptr, 0) == -ENOTTY);
}

ASTIA_TEST(uart16550_tcflsh_reports_port_failure) {
    astia::dev::pio_bus   bus;
    astia::dev::irq_line  irq;
    astia::dev::uart16550 uart("uart-test", bus, irq, fake_base);
    int                   selector = TCIFLUSH;
    ASTIA_CHECK(uart.ioctl(TCFLSH, &selector, sizeof(selector)) == -EIO);
}
