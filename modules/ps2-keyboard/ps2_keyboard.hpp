#pragma once

#include "keymap.hpp"
#include "ps2_controller.hpp"
#include "scancode.hpp"

namespace astia::ps2 {
    class ps2_keyboard {
        public:
        constexpr ps2_keyboard(ps2_controller& controller, input_sink sink)
            : _controller(controller), _sink(sink) {}

        /// Reset the keyboard and select untranslated scan-code set 2.
        ps2_error initialize();

        /// Bounded ISR drain: never waits and never blocks the controller.
        void irq();

        /// Feed one scan-code byte. Returns false only when the sink
        /// rejected at least one emitted byte.
        bool feed(u8 byte);

        const modifiers& state() const {
            return _modifiers;
        }

        private:
        static constexpr u32 max_drain_bytes = 64;

        ps2_controller& _controller;
        set2_decoder    _decoder;
        modifiers       _modifiers;
        bool            _caps_down = false;
        input_sink      _sink;
    };
} // namespace astia::ps2
