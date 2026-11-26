#pragma once

#include <astia/dev/capabilities.hpp>

#include <deque>
#include <vector>

namespace astia::test {
    struct ps2_operation {
        bool command = false;
        u8   value   = 0;
    };

    struct fake_8042 {
        u8 config = 0x45;

        bool input_full           = false;
        bool input_full_forever   = false;
        bool output_empty_forever = false;
        bool output_stuck         = false;

        bool queue_scan_before_ack               = false;
        bool interleave_scan_on_controller_reply = false;
        u32  resend_count                        = 0;
        bool no_ack                              = false;
        bool reset_sends_ack                     = true;
        bool reset_bat_first                     = false;
        bool self_test_resets_config             = false;
        u8   self_test_reply                     = 0x55;
        u8   self_test_reset_config              = 0x45;
        u8   port_test_reply                     = 0x00;
        u8   reset_bat                           = 0xAA;

        u32 status_reads   = 0;
        u32 foreign_reads  = 0;
        u32 foreign_writes = 0;

        std::deque<u8>             output;
        std::deque<u8>             output_flags;
        std::vector<u8>            keyboard_commands;
        std::vector<ps2_operation> operations;

        void queue_keyboard(u8 byte) {
            queue_keyboard_with_flags(byte, 0);
        }

        void queue_keyboard_with_flags(u8 byte, u8 flags) {
            output.push_back(byte);
            output_flags.push_back(flags);
        }

        void queue_scan() {
            queue_keyboard(0x1C);
        }

        void queue_controller_reply(u8 reply) {
            if (interleave_scan_on_controller_reply)
                queue_scan();

            queue_keyboard(reply);
        }

        void reply_command() {
            if (queue_scan_before_ack)
                queue_scan();

            if (resend_count != 0) {
                --resend_count;
                queue_keyboard(0xFE);

                return;
            }

            if (!no_ack)
                queue_keyboard(0xFA);
        }

        u8 read_status() {
            ++status_reads;

            if (output_empty_forever)
                return input_full_forever ? 0x02 : 0x00;

            u8 status = 0;

            if (!output.empty() || output_stuck)
                status |= 0x01;

            if (!output_flags.empty())
                status |= output_flags.front();

            if (input_full || input_full_forever)
                status |= 0x02;

            return status;
        }

        u8 read_data() {
            if (output.empty())
                return 0;

            const u8 value = output.front();

            if (!output_stuck) {
                output.pop_front();

                if (!output_flags.empty())
                    output_flags.pop_front();
            }

            return value;
        }

        void write_command(u8 value) {
            operations.push_back({true, value});

            switch (value) {
            case 0x20:
                queue_controller_reply(config);
                break;
            case 0x60:
                pending = pending_mode::config;

                break;
            case 0xAA:
                queue_controller_reply(self_test_reply);

                if (self_test_resets_config)
                    config = self_test_reset_config;

                break;
            case 0xAB:
                queue_controller_reply(port_test_reply);
                break;
            case 0xA7:
                config |= 0x20;

                break;
            case 0xAD:
                config |= 0x10;

                break;
            case 0xAE:
                config &= static_cast<u8>(~0x10);

                break;
            case 0xD2:
                pending = pending_mode::output;

                break;
            default:
                break;
            }
        }

        void write_data(u8 value) {
            operations.push_back({false, value});

            if (pending == pending_mode::config) {
                config  = value;
                pending = pending_mode::none;

                return;
            }

            if (pending == pending_mode::output) {
                queue_keyboard(value);

                pending = pending_mode::none;

                return;
            }

            keyboard_commands.push_back(value);

            if (expect_parameter) {
                expect_parameter = false;

                reply_command();

                return;
            }

            if (value == 0xF0) {
                expect_parameter = true;

                reply_command();

                return;
            }

            if (value == 0xFF) {
                if (reset_sends_ack && reset_bat_first) {
                    queue_keyboard(reset_bat);
                    reply_command();
                }
                else {
                    if (reset_sends_ack)
                        reply_command();

                    queue_keyboard(reset_bat);
                }

                return;
            }

            reply_command();
        }

        private:
        enum class pending_mode : u8 { none, config, output };

        pending_mode pending          = pending_mode::none;
        bool         expect_parameter = false;
    };

    inline u8 fake_read8(void* context, u16 port) {
        auto& fake = *static_cast<fake_8042*>(context);

        if (port == 0x64)
            return fake.read_status();

        if (port == 0x60)
            return fake.read_data();

        ++fake.foreign_reads;

        return 0;
    }

    inline u16 fake_read16(void* context, u16 port) {
        return static_cast<u16>(fake_read8(context, port) |
                                (fake_read8(context, static_cast<u16>(port + 1)) << 8));
    }

    inline u32 fake_read32(void* context, u16 port) {
        return static_cast<u32>(fake_read16(context, port)) |
               (static_cast<u32>(fake_read16(context, static_cast<u16>(port + 2))) << 16);
    }

    inline void fake_write8(void* context, u16 port, u8 value) {
        auto& fake = *static_cast<fake_8042*>(context);

        if (port == 0x64)
            fake.write_command(value);
        else if (port == 0x60)
            fake.write_data(value);
        else
            ++fake.foreign_writes;
    }

    inline void fake_write16(void* context, u16 port, u16 value) {
        fake_write8(context, port, static_cast<u8>(value));
        fake_write8(context, static_cast<u16>(port + 1), static_cast<u8>(value >> 8));
    }

    inline void fake_write32(void* context, u16 port, u32 value) {
        fake_write16(context, port, static_cast<u16>(value));
        fake_write16(context, static_cast<u16>(port + 2), static_cast<u16>(value >> 16));
    }

    inline dev::pio_bus fake_ps2_bus(fake_8042& fake) {
        return dev::pio_bus{&fake,       fake_read8,   fake_read16, fake_read32,
                            fake_write8, fake_write16, fake_write32};
    }
} // namespace astia::test
