#pragma once

#include <astia/dev/capabilities.hpp>

namespace astia::ps2 {
    enum class ps2_error : u8 {
        none,
        invalid,
        timeout,
        protocol,
        self_test,
        port_test,
    };

    class ps2_controller {
        public:
        static constexpr u16 data_port    = 0x60;
        static constexpr u16 command_port = 0x64;

        static constexpr u8 status_output_full = 0x01;
        static constexpr u8 status_input_full  = 0x02;
        static constexpr u8 status_system      = 0x04;
        static constexpr u8 status_command     = 0x08;
        static constexpr u8 status_key_locked  = 0x10;
        static constexpr u8 status_aux_full    = 0x20;
        static constexpr u8 status_timeout     = 0x40;
        static constexpr u8 status_parity      = 0x80;

        static constexpr u8 config_translate        = 0x40;
        static constexpr u8 config_aux_disable      = 0x20;
        static constexpr u8 config_keyboard_disable = 0x10;
        static constexpr u8 config_keyboard_irq     = 0x01;
        static constexpr u8 config_reserved_mask    = 0x88;

        static constexpr u8 command_read_config      = 0x20;
        static constexpr u8 command_write_config     = 0x60;
        static constexpr u8 command_self_test        = 0xAA;
        static constexpr u8 command_port_test        = 0xAB;
        static constexpr u8 command_disable_aux      = 0xA7;
        static constexpr u8 command_disable_keyboard = 0xAD;
        static constexpr u8 command_enable_keyboard  = 0xAE;
        static constexpr u8 command_write_output     = 0xD2;
        static constexpr u8 command_enable_scanning  = 0xF4;

        static constexpr u8 reply_ack         = 0xFA;
        static constexpr u8 reply_resend      = 0xFE;
        static constexpr u8 bat_ok            = 0xAA;
        static constexpr u8 bat_error         = 0xFC;
        static constexpr u8 keyboard_error    = 0x00;
        static constexpr u8 keyboard_error_ff = 0xFF;
        static constexpr u8 self_test_ok      = 0x55;
        static constexpr u8 port_test_ok      = 0x00;

        static constexpr u32 default_status_budget = 1u << 18;
        static constexpr u32 default_retry_limit   = 3;

        constexpr ps2_controller() = default;

        constexpr explicit ps2_controller(const dev::io_port_window& io,
                                          u32 status_budget = default_status_budget)
            : _io(&io), _budget(status_budget) {}

        /// Bind the platform window after static initialization: the module
        /// entrypoint is the first context that can call the platform.
        constexpr void attach(const dev::io_port_window& io) {
            _io = &io;
        }

        /// Controller bring-up: drain, disable the keyboard interface, read
        /// and preserve the command byte with translation and IRQ disabled,
        /// self-test, port-test, restore the preserved configuration, then
        /// re-enable the keyboard interface.
        ps2_error initialize();

        /// Drain any pending output-buffer bytes before IRQ delivery is
        /// enabled. Bounded like every other controller wait.
        ps2_error drain();

        /// Enable IRQ1 in the command byte, keeping translation disabled.
        ps2_error enable_interrupt();

        /// Clear IRQ1 in the command byte and disable translation.
        ps2_error disable_interrupt();

        /// Disable the first keyboard interface (0xAD).
        ps2_error disable_keyboard();

        /// Keyboard protocol commands. ACK/RESEND are retried a bounded
        /// number of times; non-protocol scan codes are drained, never
        /// returned as command responses.
        ps2_error keyboard_command(u8 command);
        ps2_error keyboard_command_with_data(u8 command, u8 data);
        ps2_error reset_keyboard();
        ps2_error set_scancode_set(u8 set);
        ps2_error enable_scanning();

        /// ISR-facing read: never waits, accepts only keyboard data and
        /// discards auxiliary bytes, bytes carrying parity/timeout errors,
        /// and protocol responses that are not scan codes.
        bool read_scan_code(u8& byte);

        /// Test/diagnostic only: put a byte in the controller output buffer
        /// as keyboard data, which raises IRQ1 when the command byte enables
        /// it.
        ps2_error inject_output(u8 byte);

        ps2_error last_error() const {
            return _last_error;
        }

        u8 config() const {
            return _config;
        }

        private:
        static constexpr u32 max_scan_discards = 8;

        enum class controller_reply : u8 {
            config,
            self_test,
            port_test,
        };

        bool      read_status(u8& status);
        bool      wait_input_empty();
        bool      flush_output();
        bool      write_command(u8 command);
        bool      write_data(u8 data);
        bool      read_raw(u8& value);
        ps2_error read_reply(u8& value);
        ps2_error read_controller_reply(controller_reply kind, u8& value,
                                        bool require_aux_disabled = true);
        ps2_error read_config(u8& value, bool require_aux_disabled);
        ps2_error verify_config(u8 expected, bool require_aux_disabled = true);
        ps2_error write_config(u8 value);

        ps2_error fail(ps2_error error) {
            _last_error = error;

            return error;
        }

        static bool is_protocol_byte(u8 byte);
        static bool is_config_byte(u8 byte, bool require_aux_disabled);

        const dev::io_port_window* _io           = nullptr;
        u32                        _budget       = default_status_budget;
        u32                        _retries      = default_retry_limit;
        u8                         _config       = 0;
        bool                       _config_valid = false;
        ps2_error                  _last_error   = ps2_error::none;
    };
} // namespace astia::ps2
