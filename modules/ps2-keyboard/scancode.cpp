#include "scancode.hpp"

namespace astia::ps2 {
    namespace {
        constexpr u8 prefix_break    = 0xF0;
        constexpr u8 prefix_extended = 0xE0;
        constexpr u8 prefix_pause    = 0xE1;
        constexpr u8 pause_tail[]    = {0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77};

        key decode_normal(u8 code) {
            if (code == 0x83)
                return key::f7;

            if ((code & 0x80) != 0)
                return key::none;

            switch (code) {
            case 0x01:
                return key::f9;
            case 0x02:
                return key::f7;
            case 0x03:
                return key::f5;
            case 0x04:
                return key::f3;
            case 0x05:
                return key::f1;
            case 0x06:
                return key::f2;
            case 0x07:
                return key::f12;
            case 0x09:
                return key::f10;
            case 0x0A:
                return key::f8;
            case 0x0B:
                return key::f6;
            case 0x0C:
                return key::f4;
            case 0x0D:
                return key::tab;
            case 0x0E:
                return key::backtick;
            case 0x11:
                return key::left_alt;
            case 0x12:
                return key::left_shift;
            case 0x14:
                return key::left_ctrl;
            case 0x15:
                return key::q;
            case 0x16:
                return key::digit_1;
            case 0x1A:
                return key::z;
            case 0x1B:
                return key::s;
            case 0x1C:
                return key::a;
            case 0x1D:
                return key::w;
            case 0x1E:
                return key::digit_2;
            case 0x21:
                return key::c;
            case 0x22:
                return key::x;
            case 0x23:
                return key::d;
            case 0x24:
                return key::e;
            case 0x25:
                return key::digit_4;
            case 0x26:
                return key::digit_3;
            case 0x29:
                return key::space;
            case 0x2A:
                return key::v;
            case 0x2B:
                return key::f;
            case 0x2C:
                return key::t;
            case 0x2D:
                return key::r;
            case 0x2E:
                return key::digit_5;
            case 0x31:
                return key::n;
            case 0x32:
                return key::b;
            case 0x33:
                return key::h;
            case 0x34:
                return key::g;
            case 0x35:
                return key::y;
            case 0x36:
                return key::digit_6;
            case 0x3A:
                return key::m;
            case 0x3B:
                return key::j;
            case 0x3C:
                return key::u;
            case 0x3D:
                return key::digit_7;
            case 0x3E:
                return key::digit_8;
            case 0x41:
                return key::comma;
            case 0x42:
                return key::k;
            case 0x43:
                return key::i;
            case 0x44:
                return key::o;
            case 0x45:
                return key::digit_0;
            case 0x46:
                return key::digit_9;
            case 0x49:
                return key::period;
            case 0x4A:
                return key::slash;
            case 0x4B:
                return key::l;
            case 0x4C:
                return key::semicolon;
            case 0x4D:
                return key::p;
            case 0x4E:
                return key::minus;
            case 0x52:
                return key::quote;
            case 0x54:
                return key::left_bracket;
            case 0x55:
                return key::equals;
            case 0x58:
                return key::caps_lock;
            case 0x59:
                return key::right_shift;
            case 0x5A:
                return key::enter;
            case 0x5B:
                return key::right_bracket;
            case 0x5D:
                return key::backslash;
            case 0x66:
                return key::backspace;
            case 0x69:
                return key::keypad_1;
            case 0x6B:
                return key::keypad_4;
            case 0x6C:
                return key::keypad_7;
            case 0x70:
                return key::keypad_0;
            case 0x71:
                return key::keypad_decimal;
            case 0x72:
                return key::keypad_2;
            case 0x73:
                return key::keypad_5;
            case 0x74:
                return key::keypad_6;
            case 0x75:
                return key::keypad_8;
            case 0x76:
                return key::escape;
            case 0x78:
                return key::f11;
            case 0x79:
                return key::keypad_plus;
            case 0x7A:
                return key::keypad_3;
            case 0x7B:
                return key::keypad_minus;
            case 0x7C:
                return key::keypad_multiply;
            case 0x7D:
                return key::keypad_9;
            default:
                return key::none;
            }
        }

        key decode_extended(u8 code) {
            switch (code) {
            case 0x11:
                return key::right_alt;
            case 0x14:
                return key::right_ctrl;
            case 0x1C:
                return key::keypad_enter;
            case 0x35:
                return key::keypad_divide;
            case 0x47:
                return key::home;
            case 0x48:
                return key::up;
            case 0x49:
                return key::page_up;
            case 0x4A:
                return key::keypad_divide;
            case 0x4B:
                return key::left;
            case 0x4D:
                return key::right;
            case 0x4F:
                return key::end;
            case 0x50:
                return key::down;
            case 0x51:
                return key::page_down;
            case 0x52:
                return key::insert;
            case 0x53:
                return key::delete_key;
            case 0x5A:
                return key::keypad_enter;
            case 0x69:
                return key::end;
            case 0x6B:
                return key::left;
            case 0x6C:
                return key::home;
            case 0x70:
                return key::insert;
            case 0x71:
                return key::delete_key;
            case 0x72:
                return key::down;
            case 0x74:
                return key::right;
            case 0x75:
                return key::up;
            case 0x7A:
                return key::page_down;
            case 0x7D:
                return key::page_up;
            default:
                return key::none;
            }
        }
    } // namespace

    void set2_decoder::reset() {
        _state       = state::normal;
        _pause_index = 0;
    }

    key_event set2_decoder::handle_prefix(u8 byte) {
        _state       = state::normal;
        _pause_index = 0;

        switch (byte) {
        case prefix_extended:
            _state = state::extended;

            break;
        case prefix_pause:
            _state = state::pause;

            break;
        case prefix_break:
            _state = state::break_code;

            break;
        default:
            break;
        }

        return {};
    }

    key_event set2_decoder::feed(u8 byte) {
        switch (_state) {
        case state::normal:
            if (byte == prefix_extended || byte == prefix_pause || byte == prefix_break)
                return handle_prefix(byte);

            return {decode_normal(byte), true};
        case state::break_code:
            if (byte == prefix_extended || byte == prefix_pause || byte == prefix_break)
                return handle_prefix(byte);

            _state = state::normal;

            return {decode_normal(byte), false};
        case state::extended:
            if (byte == prefix_break) {
                _state = state::extended_break;

                return {};
            }
            if (byte == prefix_extended || byte == prefix_pause)
                return handle_prefix(byte);

            _state = state::normal;

            return {decode_extended(byte), true};
        case state::extended_break:
            if (byte == prefix_extended || byte == prefix_pause)
                return handle_prefix(byte);

            if (byte == prefix_break) {
                _state = state::normal;

                return {};
            }

            _state = state::normal;

            return {decode_extended(byte), false};
        case state::pause:
            if (byte == pause_tail[_pause_index]) {
                ++_pause_index;

                if (_pause_index == sizeof(pause_tail)) {
                    _state       = state::normal;
                    _pause_index = 0;
                }

                return {};
            }
            _state       = state::normal;
            _pause_index = 0;

            return handle_prefix(byte);
        }

        return {};
    }
} // namespace astia::ps2
