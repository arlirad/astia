#pragma once

#include <astia/common.hpp>

#include "scancode.hpp"

namespace astia::ps2 {
    struct modifiers {
        bool left_shift  = false;
        bool right_shift = false;
        bool left_ctrl   = false;
        bool right_ctrl  = false;
        bool left_alt    = false;
        bool right_alt   = false;
        bool caps_lock   = false;

        constexpr bool shift() const {
            return left_shift || right_shift;
        }

        constexpr bool ctrl() const {
            return left_ctrl || right_ctrl;
        }
    };

    struct byte_sequence {
        static constexpr u8 capacity = 6;

        u8 bytes[capacity]{};
        u8 length = 0;
    };

    struct input_sink {
        void* context          = nullptr;
        bool (*put)(void*, u8) = nullptr;

        bool write(u8 byte) const {
            return put != nullptr && put(context, byte);
        }
    };

    class us_keymap {
        public:
        static bool          is_modifier(key code);
        static void          apply_modifier(key code, bool pressed, modifiers& state);
        static byte_sequence translate(key code, const modifiers& state);
        static bool          emit(key code, const modifiers& state, input_sink sink);
    };
} // namespace astia::ps2
