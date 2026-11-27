#include "keymap.hpp"

namespace astia::ps2 {
    namespace {
        constexpr byte_sequence one(u8 byte) {
            return {{byte}, 1};
        }

        byte_sequence copy(const u8* bytes, u8 length) {
            byte_sequence result;

            if (length > byte_sequence::capacity)
                length = byte_sequence::capacity;

            for (u8 i = 0; i < length; ++i) {
                result.bytes[i] = bytes[i];
            }

            result.length = length;

            return result;
        }

        byte_sequence with_ctrl_prefix(const u8* bytes, u8 length) {
            byte_sequence result;

            u8 index = 0;

            result.bytes[index++] = 0x1b;

            for (u8 i = 0; i < length && index < byte_sequence::capacity; ++i) {
                result.bytes[index++] = bytes[i];
            }

            result.length = index;

            return result;
        }

        constexpr bool is_letter(key code) {
            return static_cast<u8>(code) >= static_cast<u8>(key::a) &&
                   static_cast<u8>(code) <= static_cast<u8>(key::z);
        }

        constexpr bool is_digit(key code) {
            return static_cast<u8>(code) >= static_cast<u8>(key::digit_0) &&
                   static_cast<u8>(code) <= static_cast<u8>(key::digit_9);
        }

        constexpr u8 letter_index(key code) {
            return static_cast<u8>(static_cast<u8>(code) - static_cast<u8>(key::a));
        }

        constexpr u8 digit_index(key code) {
            return static_cast<u8>(static_cast<u8>(code) - static_cast<u8>(key::digit_0));
        }

        constexpr char shifted_digits[] = ")!@#$%^&*(";

        constexpr u8 up[]        = {0x1b, '[', 'A'};
        constexpr u8 down[]      = {0x1b, '[', 'B'};
        constexpr u8 right[]     = {0x1b, '[', 'C'};
        constexpr u8 left[]      = {0x1b, '[', 'D'};
        constexpr u8 home[]      = {0x1b, '[', 'H'};
        constexpr u8 end[]       = {0x1b, '[', 'F'};
        constexpr u8 insert[]    = {0x1b, '[', '2', '~'};
        constexpr u8 erase[]     = {0x1b, '[', '3', '~'};
        constexpr u8 page_up[]   = {0x1b, '[', '5', '~'};
        constexpr u8 page_down[] = {0x1b, '[', '6', '~'};
        constexpr u8 f1[]        = {0x1b, 'O', 'P'};
        constexpr u8 f2[]        = {0x1b, 'O', 'Q'};
        constexpr u8 f3[]        = {0x1b, 'O', 'R'};
        constexpr u8 f4[]        = {0x1b, 'O', 'S'};
        constexpr u8 f5[]        = {0x1b, '[', '1', '5', '~'};
        constexpr u8 f6[]        = {0x1b, '[', '1', '7', '~'};
        constexpr u8 f7[]        = {0x1b, '[', '1', '8', '~'};
        constexpr u8 f8[]        = {0x1b, '[', '1', '9', '~'};
        constexpr u8 f9[]        = {0x1b, '[', '2', '0', '~'};
        constexpr u8 f10[]       = {0x1b, '[', '2', '1', '~'};
        constexpr u8 f11[]       = {0x1b, '[', '2', '3', '~'};
        constexpr u8 f12[]       = {0x1b, '[', '2', '4', '~'};

        byte_sequence navigation(const u8* bytes, u8 length, const modifiers& state) {
            return state.ctrl() ? with_ctrl_prefix(bytes, length) : copy(bytes, length);
        }
    } // namespace

    bool us_keymap::is_modifier(key code) {
        switch (code) {
        case key::left_shift:
        case key::right_shift:
        case key::left_ctrl:
        case key::right_ctrl:
        case key::left_alt:
        case key::right_alt:
            return true;
        default:
            return false;
        }
    }

    void us_keymap::apply_modifier(key code, bool pressed, modifiers& state) {
        switch (code) {
        case key::left_shift:
            state.left_shift = pressed;
            break;
        case key::right_shift:
            state.right_shift = pressed;
            break;
        case key::left_ctrl:
            state.left_ctrl = pressed;
            break;
        case key::right_ctrl:
            state.right_ctrl = pressed;
            break;
        case key::left_alt:
            state.left_alt = pressed;
            break;
        case key::right_alt:
            state.right_alt = pressed;
            break;
        default:
            break;
        }
    }

    byte_sequence us_keymap::translate(key code, const modifiers& state) {
        if (state.ctrl()) {
            if (is_letter(code))
                return one(static_cast<u8>(1 + letter_index(code)));

            switch (code) {
            case key::space:
                return one(0x00);
            case key::left_bracket:
                return one(0x1b);
            case key::backslash:
                return one(0x1c);
            case key::right_bracket:
                return one(0x1d);
            default:
                break;
            }
        }

        if (is_letter(code)) {
            const bool upper = state.shift() != state.caps_lock;

            return one(static_cast<u8>((upper ? 'A' : 'a') + letter_index(code)));
        }

        if (is_digit(code)) {
            const u8 index = digit_index(code);

            return one(static_cast<u8>(state.shift() ? shifted_digits[index]
                                                     : static_cast<char>('0' + index)));
        }

        switch (code) {
        case key::backtick:
            return one(state.shift() ? '~' : '`');
        case key::minus:
            return one(state.shift() ? '_' : '-');
        case key::equals:
            return one(state.shift() ? '+' : '=');
        case key::left_bracket:
            return one(state.shift() ? '{' : '[');
        case key::right_bracket:
            return one(state.shift() ? '}' : ']');
        case key::backslash:
            return one(state.shift() ? '|' : '\\');
        case key::semicolon:
            return one(state.shift() ? ':' : ';');
        case key::quote:
            return one(state.shift() ? '"' : '\'');
        case key::comma:
            return one(state.shift() ? '<' : ',');
        case key::period:
            return one(state.shift() ? '>' : '.');
        case key::slash:
            return one(state.shift() ? '?' : '/');
        case key::escape:
            return one(0x1b);
        case key::backspace:
            return one(0x7f);
        case key::tab:
            return one('\t');
        case key::enter:
            return one('\r');
        case key::space:
            return one(' ');
        case key::insert:
            return navigation(insert, sizeof(insert), state);
        case key::delete_key:
            return navigation(erase, sizeof(erase), state);
        case key::home:
            return navigation(home, sizeof(home), state);
        case key::end:
            return navigation(end, sizeof(end), state);
        case key::page_up:
            return navigation(page_up, sizeof(page_up), state);
        case key::page_down:
            return navigation(page_down, sizeof(page_down), state);
        case key::up:
            return navigation(up, sizeof(up), state);
        case key::down:
            return navigation(down, sizeof(down), state);
        case key::left:
            return navigation(left, sizeof(left), state);
        case key::right:
            return navigation(right, sizeof(right), state);
        case key::f1:
            return navigation(f1, sizeof(f1), state);
        case key::f2:
            return navigation(f2, sizeof(f2), state);
        case key::f3:
            return navigation(f3, sizeof(f3), state);
        case key::f4:
            return navigation(f4, sizeof(f4), state);
        case key::f5:
            return navigation(f5, sizeof(f5), state);
        case key::f6:
            return navigation(f6, sizeof(f6), state);
        case key::f7:
            return navigation(f7, sizeof(f7), state);
        case key::f8:
            return navigation(f8, sizeof(f8), state);
        case key::f9:
            return navigation(f9, sizeof(f9), state);
        case key::f10:
            return navigation(f10, sizeof(f10), state);
        case key::f11:
            return navigation(f11, sizeof(f11), state);
        case key::f12:
            return navigation(f12, sizeof(f12), state);
        case key::keypad_0:
            return one('0');
        case key::keypad_1:
            return one('1');
        case key::keypad_2:
            return one('2');
        case key::keypad_3:
            return one('3');
        case key::keypad_4:
            return one('4');
        case key::keypad_5:
            return one('5');
        case key::keypad_6:
            return one('6');
        case key::keypad_7:
            return one('7');
        case key::keypad_8:
            return one('8');
        case key::keypad_9:
            return one('9');
        case key::keypad_decimal:
            return one('.');
        case key::keypad_divide:
            return one('/');
        case key::keypad_multiply:
            return one('*');
        case key::keypad_minus:
            return one('-');
        case key::keypad_plus:
            return one('+');
        case key::keypad_enter:
            return one('\r');
        default:
            return {};
        }
    }

    bool us_keymap::emit(key code, const modifiers& state, input_sink sink) {
        const byte_sequence sequence = translate(code, state);

        bool accepted = true;

        for (u8 i = 0; i < sequence.length; ++i) {
            accepted = sink.write(sequence.bytes[i]) && accepted;
        }

        return accepted;
    }
} // namespace astia::ps2
