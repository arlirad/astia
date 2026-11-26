#include "ps2_keyboard.hpp"

namespace astia::ps2 {
    ps2_error ps2_keyboard::initialize() {
        const ps2_error reset = _controller.reset_keyboard();

        if (reset != ps2_error::none)
            return reset;

        const ps2_error scancode = _controller.set_scancode_set(2);

        if (scancode != ps2_error::none)
            return scancode;

        return _controller.enable_scanning();
    }

    void ps2_keyboard::irq() {
        for (u32 drained = 0; drained < max_drain_bytes; ++drained) {
            u8 byte = 0;

            if (!_controller.read_scan_code(byte))
                break;

            (void) feed(byte);
        }
    }

    bool ps2_keyboard::feed(u8 byte) {
        const key_event event = _decoder.feed(byte);

        if (!event.valid())
            return true;

        if (event.code == key::caps_lock) {
            if (event.pressed && !_caps_down)
                _modifiers.caps_lock = !_modifiers.caps_lock;

            _caps_down = event.pressed;

            return true;
        }

        if (us_keymap::is_modifier(event.code)) {
            us_keymap::apply_modifier(event.code, event.pressed, _modifiers);

            return true;
        }

        if (!event.pressed)
            return true;

        return us_keymap::emit(event.code, _modifiers, _sink);
    }
} // namespace astia::ps2
