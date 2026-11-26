#include "ps2_controller.hpp"

namespace astia::ps2 {
    bool ps2_controller::is_protocol_byte(u8 byte) {
        return byte == reply_ack || byte == reply_resend || byte == bat_ok ||
               byte == bat_error || byte == keyboard_error || byte == keyboard_error_ff;
    }

    /// A command-byte reply is only plausible when the reserved bits are clear
    /// and the ports that must be inhibited report disabled. Before aux is
    /// disabled the capture read requires only the keyboard-disable bit; after
    /// both ports are inhibited the full filter applies.
    bool ps2_controller::is_config_byte(u8 byte, bool require_aux_disabled) {
        if ((byte & config_reserved_mask) != 0 || (byte & config_keyboard_disable) == 0)
            return false;

        return !require_aux_disabled || (byte & config_aux_disable) != 0;
    }

    bool ps2_controller::read_status(u8& status) {
        if (!_io->read_status(status)) {
            fail(ps2_error::timeout);

            return false;
        }

        return true;
    }

    bool ps2_controller::wait_input_empty() {
        for (u32 attempt = 0; attempt < _budget; ++attempt) {
            u8 status = 0;

            if (!read_status(status))
                return false;

            if ((status & status_input_full) == 0)
                return true;
        }

        fail(ps2_error::timeout);

        return false;
    }

    bool ps2_controller::flush_output() {
        for (u32 attempt = 0; attempt < _budget; ++attempt) {
            u8 status = 0;

            if (!read_status(status))
                return false;

            if ((status & status_output_full) == 0)
                return true;

            u8 ignored = 0;

            if (!_io->read_data(ignored)) {
                fail(ps2_error::timeout);

                return false;
            }
        }

        fail(ps2_error::timeout);

        return false;
    }

    bool ps2_controller::write_command(u8 command) {
        if (!wait_input_empty())
            return false;

        if (!_io->write_command(command)) {
            fail(ps2_error::timeout);

            return false;
        }

        return true;
    }

    bool ps2_controller::write_data(u8 data) {
        if (!wait_input_empty())
            return false;

        if (!_io->write_data(data)) {
            fail(ps2_error::timeout);

            return false;
        }

        return true;
    }

    bool ps2_controller::read_raw(u8& value) {
        for (u32 attempt = 0; attempt < _budget; ++attempt) {
            u8 status = 0;

            if (!read_status(status))
                return false;

            if ((status & status_output_full) == 0)
                continue;

            u8 data = 0;

            if (!_io->read_data(data)) {
                fail(ps2_error::timeout);

                return false;
            }

            if ((status & status_aux_full) != 0)
                continue;

            if ((status & (status_timeout | status_parity)) != 0) {
                fail(ps2_error::protocol);

                return false;
            }

            value = data;

            return true;
        }

        fail(ps2_error::timeout);

        return false;
    }

    ps2_error ps2_controller::read_reply(u8& value) {
        for (u32 attempt = 0; attempt < _budget; ++attempt) {
            u8 status = 0;

            if (!read_status(status))
                return _last_error;

            if ((status & status_output_full) == 0)
                continue;

            u8 data = 0;

            if (!_io->read_data(data))
                return fail(ps2_error::timeout);

            if ((status & status_aux_full) != 0)
                continue;

            if ((status & (status_timeout | status_parity)) != 0)
                return fail(ps2_error::protocol);

            if (!is_protocol_byte(data))
                continue;

            value = data;

            return ps2_error::none;
        }

        return fail(ps2_error::timeout);
    }

    ps2_error ps2_controller::read_controller_reply(controller_reply kind, u8& value,
                                                    bool require_aux_disabled) {
        for (u32 attempt = 0; attempt < _budget; ++attempt) {
            u8 status = 0;

            if (!read_status(status))
                return _last_error;

            if ((status & status_output_full) == 0)
                continue;

            u8 data = 0;

            if (!_io->read_data(data))
                return fail(ps2_error::timeout);

            if ((status & status_aux_full) != 0)
                continue;

            if ((status & (status_timeout | status_parity)) != 0)
                return fail(ps2_error::protocol);

            switch (kind) {
            case controller_reply::config:
                if (is_config_byte(data, require_aux_disabled)) {
                    value = data;

                    return ps2_error::none;
                }
                break;
            case controller_reply::self_test:
                if (data == self_test_ok) {
                    value = data;

                    return ps2_error::none;
                }
                if (data == bat_error || data == keyboard_error ||
                    data == keyboard_error_ff)
                    return fail(ps2_error::self_test);
                break;
            case controller_reply::port_test:
                if (data == port_test_ok) {
                    value = data;

                    return ps2_error::none;
                }
                if (data >= 1 && data <= 4)
                    return fail(ps2_error::port_test);
                break;
            }
        }

        return fail(ps2_error::timeout);
    }

    ps2_error ps2_controller::keyboard_command(u8 command) {
        for (u32 attempt = 0; attempt < _retries; ++attempt) {
            if (!write_data(command))
                return _last_error;

            u8 reply = 0;

            const ps2_error result = read_reply(reply);

            if (result != ps2_error::none)
                return result;

            if (reply == reply_ack)
                return ps2_error::none;

            if (reply == reply_resend)
                continue;

            return fail(ps2_error::protocol);
        }

        return fail(ps2_error::protocol);
    }

    ps2_error ps2_controller::keyboard_command_with_data(u8 command, u8 data) {
        const ps2_error command_result = keyboard_command(command);

        if (command_result != ps2_error::none)
            return command_result;

        for (u32 attempt = 0; attempt < _retries; ++attempt) {
            if (!write_data(data))
                return _last_error;

            u8 reply = 0;

            const ps2_error result = read_reply(reply);

            if (result != ps2_error::none)
                return result;

            if (reply == reply_ack)
                return ps2_error::none;

            if (reply == reply_resend)
                continue;

            return fail(ps2_error::protocol);
        }

        return fail(ps2_error::protocol);
    }

    ps2_error ps2_controller::reset_keyboard() {
        for (u32 attempt = 0; attempt < _retries; ++attempt) {
            if (!write_data(0xFF))
                return _last_error;

            bool acknowledged = false;
            bool bat_seen     = false;

            for (u32 reply_index = 0; reply_index < 4; ++reply_index) {
                u8 reply = 0;

                if (!read_raw(reply))
                    return _last_error;

                if (reply == reply_resend) {
                    if (acknowledged)
                        return fail(ps2_error::protocol);

                    break;
                }

                if (reply == reply_ack) {
                    acknowledged = true;

                    if (bat_seen)
                        return ps2_error::none;

                    continue;
                }

                if (reply == bat_ok) {
                    bat_seen = true;

                    if (acknowledged)
                        return ps2_error::none;

                    continue;
                }

                if (reply == bat_error)
                    return fail(ps2_error::self_test);

                if (reply == keyboard_error || reply == keyboard_error_ff)
                    return fail(ps2_error::protocol);
            }

            if (bat_seen && !acknowledged)
                return fail(ps2_error::protocol);
        }

        return fail(ps2_error::timeout);
    }

    ps2_error ps2_controller::set_scancode_set(u8 set) {
        if (set < 1 || set > 3)
            return fail(ps2_error::invalid);

        return keyboard_command_with_data(0xF0, set);
    }

    ps2_error ps2_controller::enable_scanning() {
        return keyboard_command(command_enable_scanning);
    }

    ps2_error ps2_controller::read_config(u8& value, bool require_aux_disabled) {
        if (!write_command(command_read_config))
            return _last_error;

        return read_controller_reply(controller_reply::config, value,
                                     require_aux_disabled);
    }

    ps2_error ps2_controller::verify_config(u8 expected, bool require_aux_disabled) {
        u8 actual = 0;

        ps2_error result = read_config(actual, require_aux_disabled);

        if (result != ps2_error::none)
            return result;

        if (actual != expected)
            return fail(ps2_error::protocol);

        return ps2_error::none;
    }

    ps2_error ps2_controller::write_config(u8 value) {
        if (!write_command(command_write_config))
            return _last_error;

        if (!write_data(value))
            return _last_error;

        _config       = value;
        _config_valid = true;

        return ps2_error::none;
    }

    ps2_error ps2_controller::initialize() {
        _last_error   = ps2_error::none;
        _config       = 0;
        _config_valid = false;

        if (_io == nullptr || !_io->valid())
            return fail(ps2_error::invalid);

        // Disable the keyboard first, then capture the preserved command byte.
        // Aux may still be enabled for this one read, so only the keyboard
        // disable bit is required; the value is cross-checked after A7 before
        // anything is written.
        if (!flush_output())
            return _last_error;

        if (!write_command(command_disable_keyboard))
            return _last_error;

        if (!flush_output())
            return _last_error;

        u8 saved = 0;

        ps2_error result = read_config(saved, false);

        if (result != ps2_error::none)
            return result;

        if (!write_command(command_disable_aux))
            return _last_error;

        if (!flush_output())
            return _last_error;

        u8 disabled = 0;

        result = read_config(disabled, true);

        if (result != ps2_error::none)
            return result;

        if (disabled != static_cast<u8>(saved | config_aux_disable))
            return fail(ps2_error::protocol);

        // Keep the original aux-enable state, but inhibit both ports for the
        // self-test and port-test transactions.
        const u8 restored =
            static_cast<u8>(saved & ~(config_translate | config_keyboard_irq));
        const u8 test = static_cast<u8>(restored | config_aux_disable);

        result = write_config(test);

        if (result != ps2_error::none)
            return result;

        result = verify_config(test);

        if (result != ps2_error::none)
            return result;

        if (!write_command(command_self_test))
            return _last_error;

        u8 reply = 0;

        result = read_controller_reply(controller_reply::self_test, reply);

        if (result != ps2_error::none)
            return result;

        // A controller self-test may reset the command byte. Reinstate the
        // inhibited configuration before the port test runs.
        result = write_config(test);

        if (result != ps2_error::none)
            return result;

        result = verify_config(test);

        if (result != ps2_error::none)
            return result;

        if (!write_command(command_port_test))
            return _last_error;

        result = read_controller_reply(controller_reply::port_test, reply);

        if (result != ps2_error::none)
            return result;

        result = write_config(restored);

        if (result != ps2_error::none)
            return result;

        result = verify_config(restored, (restored & config_aux_disable) != 0);

        if (result != ps2_error::none)
            return result;

        const u8 enabled = static_cast<u8>(restored & ~config_keyboard_disable);

        result = write_config(enabled);

        if (result != ps2_error::none)
            return result;

        if (!write_command(command_enable_keyboard))
            return _last_error;

        return ps2_error::none;
    }

    ps2_error ps2_controller::drain() {
        _last_error = ps2_error::none;

        return flush_output() ? ps2_error::none : _last_error;
    }

    ps2_error ps2_controller::enable_interrupt() {
        if (!_config_valid)
            return fail(ps2_error::invalid);

        return write_config(static_cast<u8>(_config | config_keyboard_irq));
    }

    ps2_error ps2_controller::disable_interrupt() {
        if (!_config_valid)
            return ps2_error::none;

        return write_config(static_cast<u8>(_config & ~config_keyboard_irq));
    }

    ps2_error ps2_controller::disable_keyboard() {
        if (!write_command(command_disable_keyboard))
            return _last_error;

        return drain();
    }

    bool ps2_controller::read_scan_code(u8& byte) {
        for (u32 discard = 0; discard < max_scan_discards; ++discard) {
            u8 status = 0;

            if (!read_status(status))
                return false;

            if ((status & status_output_full) == 0)
                return false;

            u8 data = 0;

            if (!_io->read_data(data)) {
                fail(ps2_error::timeout);

                return false;
            }

            if ((status & status_aux_full) != 0)
                continue;

            if ((status & (status_timeout | status_parity)) != 0) {
                fail(ps2_error::protocol);
                continue;
            }

            if (is_protocol_byte(data))
                continue;

            byte = data;

            return true;
        }

        return false;
    }

    ps2_error ps2_controller::inject_output(u8 byte) {
        if (!flush_output())
            return _last_error;

        if (!write_command(command_write_output))
            return _last_error;

        if (!write_data(byte))
            return _last_error;

        return ps2_error::none;
    }
} // namespace astia::ps2
