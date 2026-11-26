#include <astia/strings.hpp>

#include "keymap.hpp"
#include "ps2_fake_8042.hpp"
#include "ps2_keyboard.hpp"
#include "support/test.hpp"
#include <initializer_list>

using astia::dev::ps2_port_window;
using astia::ps2::input_sink;
using astia::ps2::key;
using astia::ps2::modifiers;
using astia::ps2::ps2_controller;
using astia::ps2::ps2_error;
using astia::ps2::ps2_keyboard;
using astia::ps2::us_keymap;

namespace {
    struct capture_sink {
        static constexpr astia::u32 capacity = 64;

        astia::u8  output[capacity]{};
        astia::u32 length = 0;
        bool       accept = true;
        astia::u32 calls  = 0;

        static bool put(void* context, astia::u8 byte) {
            auto& self = *static_cast<capture_sink*>(context);

            ++self.calls;

            if (!self.accept)
                return false;

            if (self.length < capacity)
                self.output[self.length++] = byte;

            return true;
        }

        astia::string_view view() const {
            return {reinterpret_cast<const char*>(output),
                    static_cast<astia::usize>(length)};
        }

        bool equals(const char* text) const {
            return view().equals(astia::string_view::from_c_str(text));
        }

        bool equals(const char* data, astia::usize size) const {
            return view().equals({data, size});
        }

        astia::u8 back() const {
            return output[length - 1];
        }

        bool empty() const {
            return length == 0;
        }

        void clear() {
            length = 0;
        }
    };

    input_sink sink_for(capture_sink& capture) {
        return {&capture, capture_sink::put};
    }

    void feed_text(ps2_keyboard& keyboard, std::initializer_list<astia::u8> bytes) {
        for (astia::u8 byte : bytes) {
            ASTIA_CHECK(keyboard.feed(byte));
        }
    }
} // namespace

ASTIA_TEST(keymap_letters_shift_caps_and_control) {
    capture_sink capture;
    modifiers    state;

    ASTIA_CHECK(us_keymap::emit(key::a, state, sink_for(capture)));
    ASTIA_CHECK(capture.equals("a"));

    state.left_shift = true;

    ASTIA_CHECK(us_keymap::emit(key::a, state, sink_for(capture)));
    ASTIA_CHECK(capture.equals("aA"));

    state.left_shift = false;
    state.caps_lock  = true;

    ASTIA_CHECK(us_keymap::emit(key::a, state, sink_for(capture)));
    ASTIA_CHECK(capture.equals("aAA"));

    state.left_shift = true;

    ASTIA_CHECK(us_keymap::emit(key::a, state, sink_for(capture)));
    ASTIA_CHECK(capture.equals("aAAa"));

    state.left_shift = false;
    state.left_ctrl  = true;

    ASTIA_CHECK(us_keymap::emit(key::a, state, sink_for(capture)));
    ASTIA_CHECK(capture.equals("aAAa\x01"));
    ASTIA_CHECK(capture.back() == 0x01);
}

ASTIA_TEST(keymap_punctuation_and_digits) {
    capture_sink capture;
    modifiers    state;

    ASTIA_CHECK(us_keymap::emit(key::digit_1, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::minus, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::equals, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::left_bracket, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::backslash, state, sink_for(capture)));
    ASTIA_CHECK(capture.equals("1-=[\\"));

    state.left_shift = true;

    ASTIA_CHECK(us_keymap::emit(key::digit_1, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::minus, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::equals, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::left_bracket, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::backslash, state, sink_for(capture)));
    ASTIA_CHECK(capture.equals("1-=[\\!_+{|"));
}

ASTIA_TEST(keymap_terminal_bytes_and_sequences) {
    capture_sink capture;
    modifiers    state;

    ASTIA_CHECK(us_keymap::emit(key::backspace, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::enter, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::tab, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::escape, state, sink_for(capture)));
    ASTIA_CHECK(capture.equals("\x7f\r\t\x1b", 4));

    ASTIA_CHECK(us_keymap::emit(key::up, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::down, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::right, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::left, state, sink_for(capture)));
    ASTIA_CHECK(capture.equals("\x7f\r\t\x1b\x1b[A\x1b[B\x1b[C\x1b[D", 16));

    ASTIA_CHECK(us_keymap::emit(key::f1, state, sink_for(capture)));
    ASTIA_CHECK(us_keymap::emit(key::f5, state, sink_for(capture)));
    ASTIA_CHECK(capture.equals("\x7f\r\t\x1b\x1b[A\x1b[B\x1b[C\x1b[D\x1bOP\x1b[15~", 24));

    state.left_ctrl = true;

    ASTIA_CHECK(us_keymap::emit(key::up, state, sink_for(capture)));
    ASTIA_CHECK(capture.equals(
        "\x7f\r\t\x1b\x1b[A\x1b[B\x1b[C\x1b[D\x1bOP\x1b[15~\x1b\x1b[A", 28));
}

ASTIA_TEST(keymap_sink_failure_is_reported) {
    capture_sink capture;

    capture.accept = false;

    modifiers state;

    ASTIA_CHECK(!us_keymap::emit(key::a, state, sink_for(capture)));
    ASTIA_CHECK(capture.empty());
    ASTIA_CHECK(capture.calls == 1);
}

ASTIA_TEST(keyboard_text_and_modifiers) {
    astia::test::fake_8042 fake;
    astia::dev::pio_bus    bus = astia::test::fake_ps2_bus(fake);
    ps2_port_window        window{bus};
    ps2_controller         controller(window, 64);
    capture_sink           capture;
    ps2_keyboard           keyboard(controller, sink_for(capture));

    feed_text(keyboard, {0x1C, 0x32, 0x21, 0x5A});
    ASTIA_CHECK(capture.equals("abc\r"));

    feed_text(keyboard, {0x12, 0x16});
    ASTIA_CHECK(capture.equals("abc\r!"));
    ASTIA_CHECK(keyboard.state().shift());

    feed_text(keyboard, {0xF0, 0x12, 0x16});
    ASTIA_CHECK(capture.equals("abc\r!1"));
    ASTIA_CHECK(!keyboard.state().shift());

    feed_text(keyboard, {0x58, 0x58, 0x1C});
    ASTIA_CHECK(capture.equals("abc\r!1A"));
    feed_text(keyboard, {0xF0, 0x58, 0x58, 0x1C});
    ASTIA_CHECK(capture.equals("abc\r!1Aa"));

    feed_text(keyboard, {0x14, 0x1C});
    ASTIA_CHECK(capture.back() == 0x01);
    feed_text(keyboard, {0xF0, 0x14, 0x1C});
    ASTIA_CHECK(capture.back() == static_cast<astia::u8>('a'));
}

ASTIA_TEST(keyboard_arrows_function_keys_and_keypad) {
    astia::test::fake_8042 fake;
    astia::dev::pio_bus    bus = astia::test::fake_ps2_bus(fake);
    ps2_port_window        window{bus};
    ps2_controller         controller(window, 64);
    capture_sink           capture;
    ps2_keyboard           keyboard(controller, sink_for(capture));

    feed_text(keyboard, {0xE0, 0x75, 0xE0, 0x72, 0xE0, 0x74, 0xE0, 0x6B});
    ASTIA_CHECK(capture.equals("\x1b[A\x1b[B\x1b[C\x1b[D"));

    capture.clear();
    feed_text(keyboard, {0x05, 0x03, 0x66, 0x5A});
    ASTIA_CHECK(capture.equals("\x1bOP\x1b[15~\x7f\r"));

    capture.clear();
    feed_text(keyboard, {0x69, 0xE0, 0x1C, 0xE0, 0x4A});
    ASTIA_CHECK(capture.equals("1\r/"));
}

ASTIA_TEST(keyboard_irq_drain_is_bounded) {
    astia::test::fake_8042 fake;

    for (astia::u32 i = 0; i < 100; ++i) {
        fake.queue_keyboard(0x1C);
    }

    astia::dev::pio_bus bus = astia::test::fake_ps2_bus(fake);
    ps2_port_window     window{bus};
    ps2_controller      controller(window, 64);
    capture_sink        capture;
    ps2_keyboard        keyboard(controller, sink_for(capture));

    keyboard.irq();
    ASTIA_CHECK(capture.calls == 64);
    keyboard.irq();
    ASTIA_CHECK(capture.calls == 100);
    keyboard.irq();
    ASTIA_CHECK(capture.calls == 100);
}

ASTIA_TEST(keyboard_initialize_resets_selects_set2_and_scans) {
    astia::test::fake_8042 fake;
    astia::dev::pio_bus    bus = astia::test::fake_ps2_bus(fake);
    ps2_port_window        window{bus};
    ps2_controller         controller(window, 64);
    capture_sink           capture;
    ps2_keyboard           keyboard(controller, sink_for(capture));

    ASTIA_CHECK(keyboard.initialize() == ps2_error::none);
    ASTIA_CHECK(fake.keyboard_commands.size() == 4);
    ASTIA_CHECK(fake.keyboard_commands[0] == 0xFF);
    ASTIA_CHECK(fake.keyboard_commands[1] == 0xF0);
    ASTIA_CHECK(fake.keyboard_commands[2] == 0x02);
    ASTIA_CHECK(fake.keyboard_commands[3] == 0xF4);
}

ASTIA_TEST(keyboard_sink_failure_propagates) {
    astia::test::fake_8042 fake;
    astia::dev::pio_bus    bus = astia::test::fake_ps2_bus(fake);
    ps2_port_window        window{bus};
    ps2_controller         controller(window, 64);
    capture_sink           capture;

    capture.accept = false;

    ps2_keyboard keyboard(controller, sink_for(capture));

    ASTIA_CHECK(!keyboard.feed(0x1C));
    ASTIA_CHECK(keyboard.feed(0xF0));
    ASTIA_CHECK(keyboard.feed(0x1C));
}
