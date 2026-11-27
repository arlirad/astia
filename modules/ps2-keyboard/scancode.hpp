#pragma once

#include <astia/common.hpp>

namespace astia::ps2 {
    enum class key : u8 {
        none,
        a,
        b,
        c,
        d,
        e,
        f,
        g,
        h,
        i,
        j,
        k,
        l,
        m,
        n,
        o,
        p,
        q,
        r,
        s,
        t,
        u,
        v,
        w,
        x,
        y,
        z,
        digit_0,
        digit_1,
        digit_2,
        digit_3,
        digit_4,
        digit_5,
        digit_6,
        digit_7,
        digit_8,
        digit_9,
        backtick,
        minus,
        equals,
        left_bracket,
        right_bracket,
        backslash,
        semicolon,
        quote,
        comma,
        period,
        slash,
        escape,
        backspace,
        tab,
        enter,
        space,
        caps_lock,
        left_shift,
        right_shift,
        left_ctrl,
        right_ctrl,
        left_alt,
        right_alt,
        f1,
        f2,
        f3,
        f4,
        f5,
        f6,
        f7,
        f8,
        f9,
        f10,
        f11,
        f12,
        insert,
        delete_key,
        home,
        end,
        page_up,
        page_down,
        up,
        down,
        left,
        right,
        keypad_0,
        keypad_1,
        keypad_2,
        keypad_3,
        keypad_4,
        keypad_5,
        keypad_6,
        keypad_7,
        keypad_8,
        keypad_9,
        keypad_decimal,
        keypad_divide,
        keypad_multiply,
        keypad_minus,
        keypad_plus,
        keypad_enter,
    };

    struct key_event {
        key  code    = key::none;
        bool pressed = false;

        constexpr bool valid() const {
            return code != key::none;
        }
    };

    class set2_decoder {
        public:
        key_event feed(u8 byte);
        void      reset();

        private:
        enum class state : u8 {
            normal,
            break_code,
            extended,
            extended_break,
            pause,
        };

        key_event handle_prefix(u8 byte);

        state _state       = state::normal;
        u32   _pause_index = 0;
    };
} // namespace astia::ps2
