#include <astia/arch/cpu.hpp>

#include "../src/proc/proc.hpp"
#include "chr/tty.hpp"
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
        read_access         = astia::proc::terminal_access_result::allowed;
        write_access        = astia::proc::terminal_access_result::allowed;
        set_access          = astia::proc::terminal_access_result::allowed;
        observed_foreground = {};
        observed_requested  = {};
    }

    class test_tty final : public astia::dev::tty {
        public:
        test_tty() : tty("test", astia::dev::dv_char) {}

        using tty::tty_receive;

        std::string output;
        astia::u8   last_signal         = 0;
        unsigned    ready_notifications = 0;

        bool receive(astia::u8 byte) {
            const astia::dev::tty_input_result result = tty_receive(byte);

            if (result == astia::dev::tty_input_result::ready)
                tty_input_ready();

            return result == astia::dev::tty_input_result::ready;
        }

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

        void tty_ready() override {
            ++ready_notifications;
        }
    };

    void set_termios(test_tty& tty, tcflag_t input, tcflag_t output, tcflag_t local) {
        struct termios value {};

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

    char result[4]{};

    tty.tty_receive('a');
    ASTIA_CHECK(tty.tty_receive('\r') == astia::dev::tty_input_result::ready);
    tty.tty_receive('\n');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 3);
    ASTIA_CHECK(result[0] == 'a' && result[1] == '\r' && result[2] == '\n');
}

ASTIA_TEST(tty_default_termios_commits_carriage_return) {
    test_tty tty;

    struct termios value {};

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

ASTIA_TEST(tty_canonical_default_reader_delivers_each_enter_once) {
    test_tty tty;

    char       result[16]{};
    const char first[]  = "echo\n";
    const char second[] = "echo\r";

    for (char byte : first) {
        if (byte != '\0')
            tty.tty_receive(static_cast<astia::u8>(byte));
    }

    for (char byte : second) {
        if (byte != '\0')
            tty.tty_receive(static_cast<astia::u8>(byte));
    }

    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 5);
    ASTIA_CHECK(std::string(result, 5) == "echo\n");
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 5);
    ASTIA_CHECK(std::string(result, 5) == "echo\n");
    ASTIA_CHECK(tty.output == "echo\necho\n");
}

ASTIA_TEST(tty_raw_zle_transition_poll_and_single_byte_reads) {
    test_tty tty;

    struct termios value {};

    ASTIA_CHECK(tty.ioctl(TCGETS, &value, sizeof(value)) == 0);

    struct termios saved = value;

    value.c_iflag     = ICRNL | INLCR;
    value.c_oflag     = OPOST | ONLCR;
    value.c_lflag     = 0;
    value.c_cc[VMIN]  = 1;
    value.c_cc[VTIME] = 0;

    ASTIA_CHECK(tty.ioctl(TCSETS, &value, sizeof(value)) == 0);

    char byte = 0;

    ASTIA_CHECK(tty.poll(POLLIN, 0) == 0);
    tty.tty_receive('e');
    ASTIA_CHECK((tty.poll(POLLIN, 0) & POLLIN) != 0);
    ASTIA_CHECK(tty.read(&byte, 1, 0) == 1 && byte == 'e');
    tty.tty_receive('\r');
    ASTIA_CHECK(tty.read(&byte, 1, 0) == 1 && byte == '\n');
    tty.tty_receive('\n');
    ASTIA_CHECK(tty.read(&byte, 1, 0) == 1 && byte == '\r');
    ASTIA_CHECK(tty.poll(POLLIN, 0) == 0);
    ASTIA_CHECK(tty.ioctl(TCSETS, &saved, sizeof(saved)) == 0);
}

ASTIA_TEST(tty_line_reader_tcsetattr_and_editing) {
    test_tty tty;

    struct termios value {};

    ASTIA_CHECK(tty.ioctl(TCGETS, &value, sizeof(value)) == 0);

    value.c_iflag |= ICRNL;
    value.c_oflag &= ~(OPOST | ONLCR);

    ASTIA_CHECK(tty.ioctl(TCSETS, &value, sizeof(value)) == 0);

    tty.tty_receive('a');
    tty.tty_receive('b');
    tty.tty_receive(127);
    tty.tty_receive('c');
    tty.tty_receive('\r');

    char result[8]{};

    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 3);
    ASTIA_CHECK(std::string(result, 3) == "ac\n");
    ASTIA_CHECK(tty.output == "ab\b \bc\n");

    ASTIA_CHECK(tty.tty_receive('\n') == astia::dev::tty_input_result::ready);
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 1);
    ASTIA_CHECK(result[0] == '\n');
    ASTIA_CHECK(tty.output == "ab\b \bc\n\n");
}

ASTIA_TEST(tty_raw_input_echoes_only_when_enabled) {
    test_tty tty;

    set_termios(tty, ICRNL, 0, 0);

    char result[8]{};

    ASTIA_CHECK(tty.tty_receive('a') == astia::dev::tty_input_result::ready);
    ASTIA_CHECK(tty.tty_receive('\r') == astia::dev::tty_input_result::ready);
    ASTIA_CHECK(tty.tty_receive('\n') == astia::dev::tty_input_result::ready);
    ASTIA_CHECK(tty.tty_receive(127) == astia::dev::tty_input_result::ready);
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 4);
    ASTIA_CHECK(result[0] == 'a' && result[1] == '\n' && result[2] == '\n' &&
                result[3] == 127);
    ASTIA_CHECK(tty.output.empty());

    struct termios value {};

    ASTIA_CHECK(tty.ioctl(TCGETS, &value, sizeof(value)) == 0);

    value.c_lflag = ECHONL;

    ASTIA_CHECK(tty.ioctl(TCSETS, &value, sizeof(value)) == 0);
    tty.tty_receive('\n');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 1 && result[0] == '\n');
    ASTIA_CHECK(tty.output == "\n");
}

ASTIA_TEST(tty_raw_repeated_byte_reads_notify_each_ready) {
    test_tty tty;

    set_termios(tty, 0, 0, 0);

    char       result  = 0;
    const char input[] = "echo\n";

    for (char byte : input) {
        if (byte == '\0')
            continue;

        ASTIA_CHECK(tty.receive(static_cast<astia::u8>(byte)));
        ASTIA_CHECK(tty.read(&result, 1, 0) == 1);
        ASTIA_CHECK(result == byte);
    }

    ASTIA_CHECK(tty.ready_notifications == sizeof(input) - 1);
}

ASTIA_TEST(tty_host_injection_canonical_fd0_shtty_alias) {
    test_tty tty;

    set_termios(tty, ICRNL, 0, ICANON | ECHO);

    test_tty& fd0   = tty;
    test_tty& shtty = tty;

    char result[16]{};

    const char input[] = "echo\n";

    for (unsigned i = 0; i < sizeof(input) - 1; ++i) {
        ASTIA_CHECK(tty.inject_input_for_test(static_cast<astia::u8>(input[i])) ==
                    (i == sizeof(input) - 2));
    }

    ASTIA_CHECK(tty.ready_notifications == 1);
    ASTIA_CHECK((fd0.poll(POLLIN, 0) & POLLIN) != 0);
    ASTIA_CHECK(fd0.read(result, sizeof(result), 0) == 5);
    ASTIA_CHECK(std::string(result, 5) == "echo\n");
    ASTIA_CHECK(shtty.read(result, sizeof(result), 0) == 0);
    ASTIA_CHECK(shtty.poll(POLLIN, 0) == 0);
    ASTIA_CHECK(tty.ready_notifications == 1);
    ASTIA_CHECK(tty.output == "echo\n");

    const char carriage_return[] = "echo\r";

    for (unsigned i = 0; i < sizeof(carriage_return) - 1; ++i) {
        ASTIA_CHECK(tty.inject_input_for_test(static_cast<astia::u8>(
                        carriage_return[i])) == (i == sizeof(carriage_return) - 2));
    }

    ASTIA_CHECK(tty.ready_notifications == 2);
    ASTIA_CHECK(shtty.read(result, sizeof(result), 0) == 5);
    ASTIA_CHECK(std::string(result, 5) == "echo\n");
    ASTIA_CHECK(fd0.poll(POLLIN, 0) == 0);
    ASTIA_CHECK(tty.output == "echo\necho\n");
}

ASTIA_TEST(tty_host_injection_raw_vmin_one_preserves_cr_lf) {
    test_tty tty;

    set_termios(tty, 0, 0, 0);

    struct termios value {};

    ASTIA_CHECK(tty.ioctl(TCGETS, &value, sizeof(value)) == 0);

    value.c_cc[VMIN]  = 1;
    value.c_cc[VTIME] = 0;

    ASTIA_CHECK(tty.ioctl(TCSETS, &value, sizeof(value)) == 0);

    test_tty& fd0   = tty;
    test_tty& shtty = tty;

    char       result  = 0;
    const char input[] = "echo\n";

    for (unsigned i = 0; i < sizeof(input) - 1; ++i) {
        ASTIA_CHECK(tty.inject_input_for_test(static_cast<astia::u8>(input[i])));
        ASTIA_CHECK((fd0.poll(POLLIN, 0) & POLLIN) != 0);
        ASTIA_CHECK(shtty.read(&result, 1, 0) == 1 && result == input[i]);
        ASTIA_CHECK(fd0.poll(POLLIN, 0) == 0);
    }

    ASTIA_CHECK(tty.ready_notifications == sizeof(input) - 1);
    ASTIA_CHECK(tty.output.empty());

    ASTIA_CHECK(tty.inject_input_for_test('\r'));
    ASTIA_CHECK(shtty.read(&result, 1, 0) == 1 && result == '\r');
    ASTIA_CHECK(tty.inject_input_for_test('\n'));
    ASTIA_CHECK(fd0.read(&result, 1, 0) == 1 && result == '\n');
}

ASTIA_TEST(tty_host_injection_vmin_vtime_wakes_once) {
    test_tty tty;

    set_termios(tty, 0, 0, 0);

    struct termios value {};

    ASTIA_CHECK(tty.ioctl(TCGETS, &value, sizeof(value)) == 0);

    value.c_cc[VMIN]  = 2;
    value.c_cc[VTIME] = 0;

    ASTIA_CHECK(tty.ioctl(TCSETS, &value, sizeof(value)) == 0);

    char result[4]{};

    tty.inject_input_for_test('e');
    ASTIA_CHECK(tty.ready_notifications == 0);
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 0);
    tty.inject_input_for_test('c');
    ASTIA_CHECK(tty.ready_notifications == 1);
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 2);
    ASTIA_CHECK(std::string(result, 2) == "ec");

    value.c_cc[VMIN]  = 2;
    value.c_cc[VTIME] = 1;

    ASTIA_CHECK(tty.ioctl(TCSETS, &value, sizeof(value)) == 0);

    astia::core::clsource request_source;
    astia::io::request    request(request_source, result, sizeof(result), 0, 1,
                                  astia::io::direction::input);

    tty.inject_input_for_test('h');
    ASTIA_CHECK(tty.submit(request) == 0 && !request.complete());
    ASTIA_CHECK(!request.timeout_set());
    ASTIA_CHECK(tty.ready_notifications == 2);
    tty.inject_input_for_test('o');
    ASTIA_CHECK(tty.submit(request) == 2);
    ASTIA_CHECK(std::string(result, 2) == "ho");
    ASTIA_CHECK(tty.ready_notifications == 3);
}

ASTIA_TEST(tty_canonical_line_notifies_once_at_enter) {
    test_tty tty;

    set_termios(tty, ICRNL, 0, ICANON);

    const char input[] = "echo\r";

    for (astia::u32 i = 0; i < sizeof(input) - 1; ++i) {
        const bool ready = tty.receive(static_cast<astia::u8>(input[i]));

        ASTIA_CHECK(ready == (i + 1 == sizeof(input) - 1));
    }

    char result[8]{};

    ASTIA_CHECK(tty.ready_notifications == 1);
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 5);
    ASTIA_CHECK(std::string(result, 5) == "echo\n");
}

ASTIA_TEST(tty_noncanonical_vmin_vtime_modes) {
    test_tty tty;

    struct termios value {};

    ASTIA_CHECK(tty.ioctl(TCGETS, &value, sizeof(value)) == 0);

    value.c_iflag = 0;
    value.c_oflag = 0;
    value.c_lflag = 0;

    char result[8]{};

    value.c_cc[VMIN]  = 0;
    value.c_cc[VTIME] = 0;

    ASTIA_CHECK(tty.ioctl(TCSETS, &value, sizeof(value)) == 0);

    astia::core::clsource immediate_source;
    astia::io::request    immediate(immediate_source, result, sizeof(result), 0, 1,
                                    astia::io::direction::input);

    ASTIA_CHECK(tty.submit(immediate) == 0 && immediate.complete());

    value.c_cc[VTIME] = 1;

    ASTIA_CHECK(tty.ioctl(TCSETS, &value, sizeof(value)) == 0);

    astia::core::clsource timed_source;
    astia::io::request    timed(timed_source, result, sizeof(result), 0, 2,
                                astia::io::direction::input);

    ASTIA_CHECK(tty.submit(timed) == 0 && !timed.complete() && timed.stage() == 1 &&
                timed.timeout_set() && timed.timeout_ns() == 100000000);
    tty.tty_receive('a');
    timed.timeout_expired(false);
    ASTIA_CHECK(tty.submit(timed) == 1 && result[0] == 'a');

    value.c_cc[VMIN]  = 2;
    value.c_cc[VTIME] = 0;

    ASTIA_CHECK(tty.ioctl(TCSETS, &value, sizeof(value)) == 0);

    astia::core::clsource minimum_source;
    astia::io::request    minimum(minimum_source, result, sizeof(result), 0, 3,
                                  astia::io::direction::input);

    tty.tty_receive('b');
    ASTIA_CHECK(tty.submit(minimum) == 0 && !minimum.complete());
    tty.tty_receive('c');
    ASTIA_CHECK(tty.submit(minimum) == 2 && result[0] == 'b' && result[1] == 'c');

    value.c_cc[VMIN]  = 2;
    value.c_cc[VTIME] = 1;

    ASTIA_CHECK(tty.ioctl(TCSETS, &value, sizeof(value)) == 0);

    astia::core::clsource interbyte_source;
    astia::io::request    interbyte(interbyte_source, result, sizeof(result), 0, 4,
                                    astia::io::direction::input);

    ASTIA_CHECK(tty.submit(interbyte) == 0 && !interbyte.timeout_set());
    tty.tty_receive('d');
    ASTIA_CHECK(tty.submit(interbyte) == 0 && interbyte.timeout_set());
    interbyte.timeout_expired(true);
    ASTIA_CHECK(tty.submit(interbyte) == 1 && result[0] == 'd');
}

ASTIA_TEST(tty_noncanonical_vmin_waits_for_minimum) {
    test_tty tty;

    set_termios(tty, 0, 0, 0);

    struct termios value {};

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

    for (unsigned i = 0; i < 1200; ++i) {
        tty.tty_receive('p');
    }

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

    struct termios value {};

    ASTIA_CHECK(tty.ioctl(TCGETS, &value, sizeof(value)) == 0);
    tty.tty_receive('x');
    ASTIA_CHECK(tty.ioctl(TCSETSF, &value, sizeof(value)) == 0);
    tty.tty_receive('\n');

    char result[8]{};

    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 1);
    ASTIA_CHECK(result[0] == '\n');
}

ASTIA_TEST(tty_tcflsh_discards_pending_input) {
    test_tty tty;

    set_termios(tty, 0, 0, 0);
    tty.tty_receive('x');

    int selector = TCIFLUSH;

    ASTIA_CHECK(tty.ioctl(TCFLSH, &selector, sizeof(selector)) == 0);

    char result[8]{};

    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 0);

    tty.tty_receive('o');
    tty.tty_receive('k');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 2);
    ASTIA_CHECK(result[0] == 'o' && result[1] == 'k');
}

ASTIA_TEST(tty_tcflsh_discards_partial_canonical_line_and_eof_state) {
    test_tty tty;

    set_termios(tty, 0, 0, ICANON);
    tty.tty_receive('a');
    tty.tty_receive('b');

    int selector = TCIFLUSH;

    ASTIA_CHECK(tty.ioctl(TCFLSH, &selector, sizeof(selector)) == 0);

    char result[8]{};

    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 0);

    tty.tty_receive('o');
    tty.tty_receive('k');
    tty.tty_receive('\n');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 3);
    ASTIA_CHECK(result[0] == 'o' && result[1] == 'k' && result[2] == '\n');

    tty.tty_receive(astia::dev::tty_defaults::ctrl_d);
    ASTIA_CHECK((tty.poll(POLLIN, 0) & POLLIN) != 0);
    ASTIA_CHECK(tty.ioctl(TCFLSH, &selector, sizeof(selector)) == 0);
    ASTIA_CHECK((tty.poll(POLLIN, 0) & POLLIN) == 0);
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 0);
}

ASTIA_TEST(tty_termios_round_trip_and_validation) {
    test_tty tty;

    struct termios next {};

    ASTIA_CHECK(tty.ioctl(TCGETS, &next, sizeof(next)) == 0);

    next.c_iflag      = ICRNL | IXON | IGNCR;
    next.c_oflag      = OPOST;
    next.c_cflag      = CS7 | CREAD | PARENB;
    next.c_lflag      = ISIG | ICANON | ECHO | ECHOE;
    next.c_cc[VERASE] = 8;
    next.c_cc[VKILL]  = 21;
    next.c_cc[VMIN]   = 4;
    next.c_cc[VTIME]  = 2;
    next.c_ispeed = next.c_ospeed = B9600;

    ASTIA_CHECK(tty.ioctl(TCSETS, &next, sizeof(next)) == 0);

    struct termios got {};

    ASTIA_CHECK(tty.ioctl(TCGETS, &got, sizeof(got)) == 0);
    ASTIA_CHECK(got.c_iflag == next.c_iflag && got.c_oflag == next.c_oflag &&
                got.c_cflag == next.c_cflag && got.c_lflag == next.c_lflag);
    ASTIA_CHECK(got.c_line == next.c_line && got.c_ispeed == next.c_ispeed &&
                got.c_ospeed == next.c_ospeed);

    for (astia::u32 i = 0; i < NCCS; ++i) {
        ASTIA_CHECK(got.c_cc[i] == next.c_cc[i]);
    }

    struct termios bad = next;

    bad.c_line = 1;

    ASTIA_CHECK(tty.ioctl(TCSETS, &bad, sizeof(bad)) == -EINVAL);

    bad          = next;
    bad.c_ospeed = B115200;

    ASTIA_CHECK(tty.ioctl(TCSETS, &bad, sizeof(bad)) == -EINVAL);
    ASTIA_CHECK(tty.ioctl(TCGETS, &got, sizeof(got)) == 0);
    ASTIA_CHECK(got.c_line == next.c_line && got.c_ispeed == next.c_ispeed &&
                got.c_ospeed == next.c_ospeed);
}

ASTIA_TEST(tty_rejects_invalid_ioctl_arguments_and_unknown_requests) {
    test_tty tty;

    struct termios value {};

    ASTIA_CHECK(tty.ioctl(TCGETS, nullptr, sizeof(value)) == -EINVAL);
    ASTIA_CHECK(tty.ioctl(TCGETS, &value, sizeof(value) - 1) == -EINVAL);
    ASTIA_CHECK(tty.ioctl(TCSETS, nullptr, sizeof(value)) == -EINVAL);
    ASTIA_CHECK(tty.ioctl(TCSETSW, &value, sizeof(value) - 1) == -EINVAL);
    ASTIA_CHECK(tty.ioctl(TCSETSF, nullptr, sizeof(value)) == -EINVAL);
    ASTIA_CHECK(tty.ioctl(TIOCGWINSZ, nullptr, sizeof(struct winsize)) == -EINVAL);
    ASTIA_CHECK(tty.ioctl(0x1234, nullptr, 0) == -ENOTTY);
}

ASTIA_TEST(tty_tcflsh_rejects_unsupported_and_invalid_requests) {
    test_tty tty;

    int selector = TCOFLUSH;

    ASTIA_CHECK(tty.ioctl(TCFLSH, &selector, sizeof(selector)) == -ENOSYS);

    selector = TCIOFLUSH;

    ASTIA_CHECK(tty.ioctl(TCFLSH, &selector, sizeof(selector)) == -ENOSYS);

    selector = 99;

    ASTIA_CHECK(tty.ioctl(TCFLSH, &selector, sizeof(selector)) == -EINVAL);
    ASTIA_CHECK(tty.ioctl(TCFLSH, nullptr, sizeof(selector)) == -EINVAL);
    ASTIA_CHECK(tty.ioctl(TCFLSH, &selector, sizeof(selector) - 1) == -EINVAL);
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

    test_tty   tty;
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
