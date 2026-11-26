#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>

#include "../src/sys/aio.hpp"
#include "chr/tty/pty.hpp"
#include "support/test.hpp"

#include <poll.h>
#include <string.h>

namespace astia::arch {
    static bool interrupts_open = true;

    void cpu::interrupts() {
        interrupts_open = true;
    }

    void cpu::nointerrupts() {
        interrupts_open = false;
    }

    bool cpu::check_interrupts() {
        return interrupts_open;
    }
}

namespace astia::sys {
    void io_device_ready_context(const io_device*) {}
}

namespace astia::dev {
    void pty_release_slot(u32, pty*) {}
}

namespace {
    void raw(astia::dev::pty& terminal) {
        struct termios value {};

        ASTIA_CHECK(terminal.ioctl(TCGETS, &value, sizeof value) == 0);

        value.c_iflag     = 0;
        value.c_oflag     = 0;
        value.c_lflag     = 0;
        value.c_cc[VMIN]  = 1;
        value.c_cc[VTIME] = 0;

        ASTIA_CHECK(terminal.ioctl(TCSETS, &value, sizeof value) == 0);

        int lock = 0;

        ASTIA_CHECK(terminal.master_ioctl(TIOCSPTLCK, &lock, sizeof lock) == 0);
    }
}

ASTIA_TEST(pty_master_slave_data_and_termios) {
    astia::dev::pty terminal(0);

    char buffer[32]{};

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

    char first_byte  = 0;
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

ASTIA_TEST(pty_master_write_returns_short_count_when_slave_input_queue_full) {
    astia::dev::pty terminal(4);

    raw(terminal);
    ASTIA_CHECK(terminal.open_slave());

    constexpr astia::u32 total = 4096;

    char input[total];

    memset(input, 'w', sizeof input);

    astia::u32 offset       = 0;
    astia::u32 short_writes = 0;

    while (offset < total) {
        const long count = terminal.master_write(input + offset, total - offset);

        ASTIA_CHECK(count > 0);

        if (count <= 0)
            break;

        const astia::u32 accepted = static_cast<astia::u32>(count);

        if (accepted < total - offset)
            ++short_writes;

        char       output[total]{};
        const long delivered = terminal.read(output, sizeof output, 0);

        ASTIA_CHECK(delivered == count);

        for (astia::u32 i = 0; i < accepted; ++i) {
            ASTIA_CHECK(output[i] == 'w');
        }

        offset += accepted;
    }

    ASTIA_CHECK(offset == total);
    ASTIA_CHECK(short_writes != 0);
}

ASTIA_TEST(pty_master_submit_parks_when_slave_input_queue_full) {
    astia::dev::pty terminal(5);

    raw(terminal);
    ASTIA_CHECK(terminal.open_slave());

    const char byte = 'f';

    astia::u32 queued = 0;

    while (terminal.master_write(&byte, 1) == 1) {
        ++queued;
    }

    ASTIA_CHECK(queued > 0);

    char pending = 'z';

    astia::io::request request(&pending, 1, 0, 0, astia::io::direction::output);

    ASTIA_CHECK(terminal.master_submit(request) == 0);
    ASTIA_CHECK(request.stage() == 1);

    char       drained[4096]{};
    const long count = terminal.read(drained, sizeof drained, 0);

    ASTIA_CHECK(count == static_cast<long>(queued));
    ASTIA_CHECK(terminal.master_submit(request) == 1);
}

ASTIA_TEST(pty_master_write_counts_consumed_flow_control_bytes) {
    astia::dev::pty terminal(6);

    int lock = 0;

    ASTIA_CHECK(terminal.master_ioctl(TIOCSPTLCK, &lock, sizeof lock) == 0);
    ASTIA_CHECK(terminal.open_slave());

    const char input[] = {'\x13', 'a', 'b', 'c'};

    ASTIA_CHECK(terminal.master_write(input, sizeof input) ==
                static_cast<long>(sizeof input));

    const char resume = '\x11';

    ASTIA_CHECK(terminal.master_write(&resume, 1) == 1);
}
