#include "chr/tty.hpp"
#include "support/test.hpp"
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

    class test_tty final : public astia::dev::tty {
        public:
        test_tty() : tty("test", astia::dev::dv_char) {}

        using tty::tty_receive;

        std::string output;

        protected:
        bool tty_putc(astia::u8 byte) override {
            output.push_back(static_cast<char>(byte));
            return true;
        }
        long tty_speed(speed_t) override {
            return 0;
        }
    };

    void set_termios(test_tty& tty, tcflag_t input, tcflag_t output, tcflag_t local) {
        struct termios value{};
        ASTIA_CHECK(tty.ioctl(TCGETS, &value) == 0);
        value.c_iflag = input;
        value.c_oflag = output;
        value.c_lflag = local;
        ASTIA_CHECK(tty.ioctl(TCSETS, &value) == 0);
    }

} // namespace

ASTIA_TEST(tty_raw_mode_delivers_bytes) {
    test_tty tty;
    char     result[4]{};
    tty.tty_receive('a');
    tty.tty_receive('\n');
    ASTIA_CHECK(tty.read(result, sizeof(result), 0) == 2);
    ASTIA_CHECK(result[0] == 'a' && result[1] == '\n');
}

ASTIA_TEST(tty_noncanonical_vmin_waits_for_minimum) {
    test_tty tty;
    set_termios(tty, 0, 0, 0);
    struct termios value{};
    ASTIA_CHECK(tty.ioctl(TCGETS, &value) == 0);
    value.c_cc[VMIN] = 2;
    ASTIA_CHECK(tty.ioctl(TCSETS, &value) == 0);

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
