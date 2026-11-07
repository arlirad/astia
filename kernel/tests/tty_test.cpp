#include "chr/tty.hpp"
#include "support/test.hpp"
#include "../src/proc/proc.hpp"
#include <astia/arch/cpu.hpp>

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
} // namespace astia::arch

namespace {

    astia::proc::terminal_access_result read_access =
        astia::proc::terminal_access_result::allowed;
    astia::proc::terminal_access_result write_access =
        astia::proc::terminal_access_result::allowed;
    astia::proc::terminal_access_result set_access =
        astia::proc::terminal_access_result::allowed;
    astia::pgrp_t observed_foreground{};
    astia::pgrp_t observed_requested{};

    void reset_terminal_access() {
        read_access           = astia::proc::terminal_access_result::allowed;
        write_access          = astia::proc::terminal_access_result::allowed;
        set_access            = astia::proc::terminal_access_result::allowed;
        observed_foreground   = {};
        observed_requested    = {};
    }

    class test_tty final : public astia::dev::tty {
        public:
        test_tty() : tty("test", astia::dev::dv_char) {}

        using tty::tty_receive;

        std::string output;
        astia::u8   last_signal = 0;

        protected:
        bool tty_putc(astia::u8 byte) override {
            output.push_back(static_cast<char>(byte));
            return true;
        }
        long tty_speed(speed_t) override {
            return 0;
        }
        void tty_signal(astia::u8 byte) override {
            last_signal = byte;
        }
    };

    void set_termios(test_tty& tty, tcflag_t input, tcflag_t output, tcflag_t local) {
        struct termios value{};
        ASTIA_CHECK(tty.ioctl(TCGETS, &value, sizeof(value)) == 0);
        value.c_iflag = input;
        value.c_oflag = output;
        value.c_lflag = local;
        ASTIA_CHECK(tty.ioctl(TCSETS, &value, sizeof(value)) == 0);
    }

} // namespace

namespace astia::proc {

    terminal_access_result terminal_read_allowed(pgrp_t) {
        return ::read_access;
    }

    terminal_access_result terminal_write_allowed(pgrp_t, bool) {
        return ::write_access;
    }

    terminal_access_result terminal_set_foreground_allowed(pgrp_t foreground,
                                                            pgrp_t requested) {
        ::observed_foreground = foreground;
        ::observed_requested  = requested;
        return ::set_access;
    }

} // namespace astia::proc

ASTIA_TEST(tty_raw_mode_delivers_bytes) {
    test_tty tty;
    set_termios(tty, 0, 0, 0);
    char     result[4]{};
    tty.tty_receive('a');
    tty.tty_receive('\r');
    tty.tty_receive('\n');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 3);
    ASTIA_CHECK(result[0] == 'a' && result[1] == '\r' && result[2] == '\n');
}

ASTIA_TEST(tty_default_termios_commits_carriage_return) {
    test_tty tty;
    struct termios value{};
    ASTIA_CHECK(tty.ioctl(TCGETS, &value, sizeof(value)) == 0);
    ASTIA_CHECK(value.c_iflag == (ICRNL | IXON));
    ASTIA_CHECK(value.c_oflag == (OPOST | ONLCR));
    ASTIA_CHECK(value.c_cflag == (CS8 | CREAD | CLOCAL));
    ASTIA_CHECK(value.c_lflag == (ISIG | ICANON | ECHO | ECHOE | ECHOK));

    char result[8]{};
    tty.tty_receive('u');
    tty.tty_receive('\r');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 2);
    ASTIA_CHECK(result[0] == 'u' && result[1] == '\n');
    ASTIA_CHECK(tty.output == "u\n");
}

ASTIA_TEST(tty_noncanonical_vmin_waits_for_minimum) {
    test_tty tty;
    set_termios(tty, 0, 0, 0);
    struct termios value{};
    ASTIA_CHECK(tty.ioctl(TCGETS, &value, sizeof(value)) == 0);
    value.c_cc[VMIN] = 2;
    ASTIA_CHECK(tty.ioctl(TCSETS, &value, sizeof(value)) == 0);

    char result[4]{};
    tty.tty_receive('a');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 0);
    tty.tty_receive('b');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 2);
    ASTIA_CHECK(result[0] == 'a' && result[1] == 'b');
}

ASTIA_TEST(tty_canonical_mode_waits_for_line) {
    test_tty tty;
    set_termios(tty, ICRNL, OPOST | ONLCR, ICANON | ECHO | ECHOE | ECHOK);

    char result[8]{};
    tty.tty_receive('a');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 0);
    tty.tty_receive('\r');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 2);
    ASTIA_CHECK(result[0] == 'a' && result[1] == '\n');
    ASTIA_CHECK(tty.output == "a\n");
}

ASTIA_TEST(tty_canonical_editing_and_output_processing) {
    test_tty tty;
    set_termios(tty, 0, OPOST | ONLCR, ICANON | ECHO | ECHOE | ECHOK);

    char result[8]{};
    tty.tty_receive('a');
    tty.tty_receive('b');
    tty.tty_receive(127);
    tty.tty_receive('\n');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 2);
    ASTIA_CHECK(result[0] == 'a' && result[1] == '\n');
    ASTIA_CHECK(tty.output == "ab\b \b\n");

    tty.output.clear();
    const char text[] = "x\n";
    ASTIA_CHECK(tty.write(text, 2, 0) == 2);
    ASTIA_CHECK(tty.output == "x\r\n");
}

ASTIA_TEST(tty_eof_and_software_flow_control) {
    test_tty tty;
    set_termios(tty, IXON, 0, ICANON);

    char result[8]{};
    tty.tty_receive('a');
    tty.tty_receive(4);
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 1);
    ASTIA_CHECK(result[0] == 'a');

    const char text[] = "x";
    tty.tty_receive(19);
    ASTIA_CHECK(tty.write(text, 1, 0) == 0);
    tty.tty_receive(17);
    ASTIA_CHECK(tty.write(text, 1, 0) == 1);
}

ASTIA_TEST(tty_canonical_overflow_does_not_join_next_line) {
    test_tty tty;
    set_termios(tty, 0, 0, ICANON);
    char result[8]{};
    for (unsigned i = 0; i < 1200; ++i)
        tty.tty_receive('p');
    tty.tty_receive('\n');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 1);
    ASTIA_CHECK(result[0] == '\n');
    tty.tty_receive('i');
    tty.tty_receive('\n');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 2);
    ASTIA_CHECK(result[0] == 'i' && result[1] == '\n');
}

ASTIA_TEST(tty_signals_discard_the_current_line) {
    test_tty tty;
    set_termios(tty, 0, 0, ICANON | ISIG);
    char result[8]{};
    tty.tty_receive('a');
    tty.tty_receive(3); // VINTR
    tty.tty_receive('\n');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 1);
    ASTIA_CHECK(result[0] == '\n');
}

ASTIA_TEST(tty_empty_eof_is_a_zero_length_read) {
    test_tty tty;
    set_termios(tty, 0, 0, ICANON);
    char result[8]{};
    tty.tty_receive(4); // VEOF
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 0);
    tty.tty_receive('x');
    tty.tty_receive('\n');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 2);
    ASTIA_CHECK(result[0] == 'x' && result[1] == '\n');
}

ASTIA_TEST(tty_tcsetsf_flushes_pending_input) {
    test_tty tty;
    set_termios(tty, 0, 0, ICANON);
    struct termios value{};
    ASTIA_CHECK(tty.ioctl(TCGETS, &value, sizeof(value)) == 0);
    tty.tty_receive('x');
    ASTIA_CHECK(tty.ioctl(TCSETSF, &value, sizeof(value)) == 0);
    tty.tty_receive('\n');
    char result[8]{};
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 1);
    ASTIA_CHECK(result[0] == '\n');
}

ASTIA_TEST(tty_process_group_round_trip) {
    test_tty   tty;
    astia::i32 group = 42;
    ASTIA_CHECK(tty.ioctl(TIOCSPGRP, &group, sizeof(group)) == 0);
    group = 0;
    ASTIA_CHECK(tty.ioctl(TIOCGPGRP, &group, sizeof(group)) == 0);
    ASTIA_CHECK(group == 42);
}

ASTIA_TEST(tty_job_control_controls_and_signals_foreground_group) {
    test_tty tty;
    set_termios(tty, 0, 0, ICANON | ISIG);
    astia::i32 group = 7;
    ASTIA_CHECK(tty.ioctl(TIOCSPGRP, &group, sizeof(group)) == 0);
    ASTIA_CHECK(tty.ioctl(TIOCSCTTY, nullptr, 0) == 0);
    tty.tty_receive('x');
    tty.tty_receive(astia::dev::tty_defaults::ctrl_z);
    char result[4]{};
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 0);
    ASTIA_CHECK(tty.last_signal == astia::dev::tty_defaults::ctrl_z);

    tty.tty_receive('y');
    tty.tty_receive(astia::dev::tty_defaults::ctrl_c);
    ASTIA_CHECK(tty.last_signal == astia::dev::tty_defaults::ctrl_c);
}

ASTIA_TEST(tty_background_io_rejects_before_transfer) {
    reset_terminal_access();
    test_tty tty;
    set_termios(tty, 0, 0, 0);
    astia::i32 group = 9;
    ASTIA_CHECK(tty.ioctl(TIOCSPGRP, &group, sizeof(group)) == 0);

    char input[2]{};
    tty.tty_receive('x');
    read_access = astia::proc::terminal_access_result::io_error;
    ASTIA_CHECK(tty.read(input, sizeof(input), 0) == -EIO);
    read_access = astia::proc::terminal_access_result::allowed;
    ASTIA_CHECK(tty.read(input, sizeof(input), 0) == 1);
    ASTIA_CHECK(input[0] == 'x');
    tty.output.clear();

    const char output[] = "y";
    write_access = astia::proc::terminal_access_result::io_error;
    ASTIA_CHECK(tty.write(output, 1, 0) == -EIO);
    ASTIA_CHECK(tty.output.empty());
    write_access = astia::proc::terminal_access_result::allowed;
    ASTIA_CHECK(tty.write(output, 1, 0) == 1);
    ASTIA_CHECK(tty.output == "y");
}

ASTIA_TEST(tty_background_signal_interrupts_io) {
    reset_terminal_access();
    test_tty tty;
    set_termios(tty, 0, 0, 0);
    astia::i32 group = 9;
    ASTIA_CHECK(tty.ioctl(TIOCSPGRP, &group, sizeof(group)) == 0);

    char input[2]{};
    tty.tty_receive('x');
    read_access = astia::proc::terminal_access_result::interrupted;
    ASTIA_CHECK(tty.read(input, sizeof(input), 0) == -EINTR);
    tty.output.clear();
    write_access = astia::proc::terminal_access_result::retry;
    const char output[] = "y";
    ASTIA_CHECK(tty.write(output, 1, 0) == -EINTR);
    ASTIA_CHECK(tty.output.empty());
}

ASTIA_TEST(tty_tcsetpgrp_background_signal_is_not_applied) {
    reset_terminal_access();
    test_tty tty;
    astia::i32 group = 7;
    ASTIA_CHECK(tty.ioctl(TIOCSPGRP, &group, sizeof(group)) == 0);
    group      = 8;
    set_access = astia::proc::terminal_access_result::io_error;
    ASTIA_CHECK(tty.ioctl(TIOCSPGRP, &group, sizeof(group)) == -EIO);
    ASTIA_CHECK(observed_foreground == astia::pgrp_t{7});
    ASTIA_CHECK(observed_requested == astia::pgrp_t{8});
    group = 0;
    ASTIA_CHECK(tty.ioctl(TIOCGPGRP, &group, sizeof(group)) == 0);
    ASTIA_CHECK(group == 7);

    set_access = astia::proc::terminal_access_result::interrupted;
    group      = 8;
    ASTIA_CHECK(tty.ioctl(TIOCSPGRP, &group, sizeof(group)) == -EINTR);
    ASTIA_CHECK(tty.ioctl(TIOCGPGRP, &group, sizeof(group)) == 0);
    ASTIA_CHECK(group == 7);
}
