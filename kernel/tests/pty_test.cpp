#include "chr/tty/pty.hpp"
#include "support/test.hpp"
#include "../src/sys/aio.hpp"
#include <astia/arch/cpu.hpp>
#include <astia/api/errno.h>
#include <poll.h>

#include <string.h>

namespace astia::arch {
    static bool interrupts_open = true;
    void cpu::interrupts() { interrupts_open = true; }
    void cpu::nointerrupts() { interrupts_open = false; }
    bool cpu::check_interrupts() { return interrupts_open; }
}

namespace astia::sys {
    void io_device_ready_context(const io_device*) {}
}

namespace astia::dev {
    void pty_release_slot(u32, pty*) {}
}

namespace {
    void raw(astia::dev::pty& terminal) {
        struct termios value{};
        ASTIA_CHECK(terminal.ioctl(TCGETS, &value, sizeof value) == 0);
        value.c_iflag = 0;
        value.c_oflag = 0;
        value.c_lflag = 0;
        value.c_cc[VMIN] = 1;
        value.c_cc[VTIME] = 0;
        ASTIA_CHECK(terminal.ioctl(TCSETS, &value, sizeof value) == 0);
        int lock = 0;
        ASTIA_CHECK(terminal.master_ioctl(TIOCSPTLCK, &lock, sizeof lock) == 0);
    }
}

ASTIA_TEST(pty_master_slave_data_and_termios) {
    astia::dev::pty terminal(0);
    char            buffer[32]{};
    raw(terminal);
    ASTIA_CHECK(terminal.open_slave());
    ASTIA_CHECK(terminal.master_write("input", 5) == 5);
    ASTIA_CHECK(terminal.read(buffer, sizeof buffer, 0) == 5);
    ASTIA_CHECK(memcmp(buffer, "input", 5) == 0);
    ASTIA_CHECK(terminal.write("output", 6, 0) == 6);
    ASTIA_CHECK(terminal.master_read(buffer, sizeof buffer) == 6);
    ASTIA_CHECK(memcmp(buffer, "output", 6) == 0);
}

ASTIA_TEST(pty_multiple_instances_are_independent) {
    astia::dev::pty first(1);
    astia::dev::pty second(2);
    raw(first);
    raw(second);
    ASTIA_CHECK(first.open_slave());
    ASTIA_CHECK(second.open_slave());
    ASTIA_CHECK(first.master_write("a", 1) == 1);
    ASTIA_CHECK(second.master_write("b", 1) == 1);
    char first_byte = 0;
    char second_byte = 0;
    ASTIA_CHECK(first.read(&first_byte, 1, 0) == 1 && first_byte == 'a');
    ASTIA_CHECK(second.read(&second_byte, 1, 0) == 1 && second_byte == 'b');
}

ASTIA_TEST(pty_hangup_returns_eio_and_poll_hup) {
    astia::dev::pty terminal(3);
    raw(terminal);
    ASTIA_CHECK(terminal.open_slave());
    terminal.master_closed();
    char byte = 0;
    ASTIA_CHECK(terminal.read(&byte, 1, 0) == -EIO);
    ASTIA_CHECK(terminal.write(&byte, 1, 0) == -EIO);
    ASTIA_CHECK((terminal.poll(POLLIN, 0) & POLLHUP) != 0);
}
