#include <astia/arch/cpu.hpp>

#include "../src/proc/proc.hpp"
#include "../src/sys/aio.hpp"
#include "chr/tty/virtual_tty.hpp"
#include "support/test.hpp"

#include <poll.h>
#include <string>

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
}

namespace astia::proc {
    terminal_access_result terminal_read_allowed(pgrp_t) {
        return terminal_access_result::allowed;
    }

    terminal_access_result terminal_write_allowed(pgrp_t, bool) {
        return terminal_access_result::allowed;
    }

    terminal_access_result terminal_set_foreground_allowed(pgrp_t, pgrp_t) {
        return terminal_access_result::allowed;
    }
}

namespace astia::sys {
    void io_device_ready_context(const io_device*) {}
}

namespace astia::fs {
    mm::shptr<open_file> open(const char*, u32) {
        return {};
    }

    long register_device(const char*, const mm::shptr<file>&) {
        return 0;
    }
}

namespace {
    class test_console final : public astia::dev::console_backend {
        public:
        bool write_byte(astia::u8 byte) override {
            output.push_back(static_cast<char>(byte));
            return true;
        }

        std::string output;
    };

    void set_raw(astia::dev::virtual_tty& tty) {
        struct termios value {};

        ASTIA_CHECK(tty.ioctl(TCGETS, &value, sizeof(value)) == 0);

        value.c_iflag     = 0;
        value.c_oflag     = 0;
        value.c_lflag     = 0;
        value.c_cc[VMIN]  = 1;
        value.c_cc[VTIME] = 0;

        ASTIA_CHECK(tty.ioctl(TCSETS, &value, sizeof(value)) == 0);
    }
}

ASTIA_TEST(virtual_tty_rejects_output_without_console) {
    astia::dev::virtual_tty tty{"test-ttv"};

    const char value[] = "x";

    ASTIA_CHECK(tty.write(value, 1, 0) == -ENODEV);
    ASTIA_CHECK(tty.poll(POLLOUT, 0) == 0);
}

ASTIA_TEST(virtual_tty_has_independent_state_and_console_output) {
    test_console sink;

    ASTIA_CHECK(astia::dev::register_console("/dev/console", &sink));

    astia::dev::virtual_tty tty{"test-ttv"};

    astia::dev::virtual_tty& first_open  = tty;
    astia::dev::virtual_tty& second_open = tty;

    set_raw(first_open);

    const char text[] = "x\n";

    ASTIA_CHECK(first_open.write(text, 2, 0) == 2);
    ASTIA_CHECK(sink.output == "x\n");

    ASTIA_CHECK(second_open.inject_input('a'));
    ASTIA_CHECK((first_open.poll(POLLIN, 0) & POLLIN) != 0);

    char result[2]{};

    ASTIA_CHECK(first_open.read(result, sizeof(result), 0) == 1);
    ASTIA_CHECK(result[0] == 'a');
    ASTIA_CHECK(second_open.poll(POLLIN, 0) == 0);
}

ASTIA_TEST(virtual_tty_ioctl_owns_foreground_group) {
    astia::dev::virtual_tty tty{"test-ttv"};
    astia::i32              group = 23;

    ASTIA_CHECK(tty.ioctl(TIOCSPGRP, &group, sizeof(group)) == 0);

    group = 0;

    ASTIA_CHECK(tty.ioctl(TIOCGPGRP, &group, sizeof(group)) == 0);
    ASTIA_CHECK(group == 23);
}
