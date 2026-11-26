#include "ps2_controller.hpp"
#include "ps2_fake_8042.hpp"
#include "support/test.hpp"

#include <vector>

using astia::dev::ps2_port_window;
using astia::ps2::ps2_controller;
using astia::ps2::ps2_error;

namespace {
    bool operation_at(const std::vector<astia::test::ps2_operation>& operations,
                      astia::usize index, bool command, astia::u8 value) {
        return index < operations.size() && operations[index].command == command &&
               operations[index].value == value;
    }

    ps2_port_window window_for(astia::test::fake_8042& fake, astia::dev::pio_bus& bus) {
        bus = astia::test::fake_ps2_bus(fake);

        return ps2_port_window{bus};
    }
} // namespace

ASTIA_TEST(ps2_controller_initializes_in_order_and_preserves_config) {
    astia::test::fake_8042 fake;

    fake.config = 0x65;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 64);

    ASTIA_CHECK(controller.initialize() == ps2_error::none);
    ASTIA_CHECK(controller.config() == 0x24);
    ASTIA_CHECK(fake.operations.size() == 18);
    ASTIA_CHECK(operation_at(fake.operations, 0, true, 0xAD));
    ASTIA_CHECK(operation_at(fake.operations, 1, true, 0x20));
    ASTIA_CHECK(operation_at(fake.operations, 2, true, 0xA7));
    ASTIA_CHECK(operation_at(fake.operations, 3, true, 0x20));
    ASTIA_CHECK(operation_at(fake.operations, 4, true, 0x60));
    ASTIA_CHECK(operation_at(fake.operations, 5, false, 0x34));
    ASTIA_CHECK(operation_at(fake.operations, 6, true, 0x20));
    ASTIA_CHECK(operation_at(fake.operations, 7, true, 0xAA));
    ASTIA_CHECK(operation_at(fake.operations, 8, true, 0x60));
    ASTIA_CHECK(operation_at(fake.operations, 9, false, 0x34));
    ASTIA_CHECK(operation_at(fake.operations, 10, true, 0x20));
    ASTIA_CHECK(operation_at(fake.operations, 11, true, 0xAB));
    ASTIA_CHECK(operation_at(fake.operations, 12, true, 0x60));
    ASTIA_CHECK(operation_at(fake.operations, 13, false, 0x34));
    ASTIA_CHECK(operation_at(fake.operations, 14, true, 0x20));
    ASTIA_CHECK(operation_at(fake.operations, 15, true, 0x60));
    ASTIA_CHECK(operation_at(fake.operations, 16, false, 0x24));
    ASTIA_CHECK(operation_at(fake.operations, 17, true, 0xAE));

    ASTIA_CHECK(controller.enable_interrupt() == ps2_error::none);
    ASTIA_CHECK(controller.config() == 0x25);
    ASTIA_CHECK((controller.config() & ps2_controller::config_translate) == 0);
    ASTIA_CHECK(fake.operations.size() == 20);
    ASTIA_CHECK(operation_at(fake.operations, 18, true, 0x60));
    ASTIA_CHECK(operation_at(fake.operations, 19, false, 0x25));
}

ASTIA_TEST(ps2_controller_preserves_enabled_aux_port) {
    astia::test::fake_8042 fake;

    fake.config = 0x45;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 64);

    ASTIA_CHECK(controller.initialize() == ps2_error::none);
    ASTIA_CHECK(controller.config() == 0x04);
    ASTIA_CHECK((controller.config() & ps2_controller::config_aux_disable) == 0);
    ASTIA_CHECK(fake.config == 0x04);
}

ASTIA_TEST(ps2_controller_reapplies_disabled_config_after_self_test) {
    astia::test::fake_8042 fake;

    fake.config                  = 0x65;
    fake.self_test_resets_config = true;
    fake.self_test_reset_config  = 0x45;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 64);

    ASTIA_CHECK(controller.initialize() == ps2_error::none);
    ASTIA_CHECK(controller.config() == 0x24);

    astia::usize self_test = fake.operations.size();

    for (astia::usize i = 0; i < fake.operations.size(); ++i) {
        if (fake.operations[i].command && fake.operations[i].value == 0xAA) {
            self_test = i;

            break;
        }
    }

    ASTIA_CHECK(self_test + 4 < fake.operations.size());
    ASTIA_CHECK(operation_at(fake.operations, self_test + 1, true, 0x60));
    ASTIA_CHECK(operation_at(fake.operations, self_test + 2, false, 0x34));
    ASTIA_CHECK(operation_at(fake.operations, self_test + 3, true, 0x20));
    ASTIA_CHECK(operation_at(fake.operations, self_test + 4, true, 0xAB));
}

ASTIA_TEST(ps2_controller_rejects_self_test_failure) {
    astia::test::fake_8042 fake;

    fake.self_test_reply = 0xFC;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 64);

    ASTIA_CHECK(controller.initialize() == ps2_error::self_test);
    ASTIA_CHECK(controller.last_error() == ps2_error::self_test);
}

ASTIA_TEST(ps2_controller_rejects_port_test_failure) {
    astia::test::fake_8042 fake;

    fake.port_test_reply = 0x01;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 64);

    ASTIA_CHECK(controller.initialize() == ps2_error::port_test);
    ASTIA_CHECK(controller.last_error() == ps2_error::port_test);
}

ASTIA_TEST(ps2_controller_keeps_both_ports_disabled_through_self_test) {
    astia::test::fake_8042 fake;

    fake.config = 0x65;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 64);

    ASTIA_CHECK(controller.initialize() == ps2_error::none);
    ASTIA_CHECK(operation_at(fake.operations, 0, true, 0xAD));
    ASTIA_CHECK(operation_at(fake.operations, 1, true, 0x20));
    ASTIA_CHECK(operation_at(fake.operations, 2, true, 0xA7));

    astia::u32 config_writes = 0;

    for (astia::usize i = 0; i + 1 < fake.operations.size(); ++i) {
        if (!fake.operations[i].command || fake.operations[i].value != 0x60)
            continue;

        const astia::u8 value = fake.operations[i + 1].value;

        ++config_writes;

        if (config_writes <= 3) {
            ASTIA_CHECK((value & 0x30) == 0x30);
        }
        else {
            ASTIA_CHECK((value & 0x30) != 0x30);
        }
    }

    ASTIA_CHECK(config_writes == 4);
}

ASTIA_TEST(ps2_controller_ignores_scan_bytes_during_controller_replies) {
    astia::test::fake_8042 fake;

    fake.config                              = 0x65;
    fake.interleave_scan_on_controller_reply = true;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 256);

    ASTIA_CHECK(controller.initialize() == ps2_error::none);
    ASTIA_CHECK(controller.config() == 0x24);
    ASTIA_CHECK(fake.output.empty());

    for (const auto& operation : fake.operations) {
        if (!operation.command && operation.value == 0x1C)
            ASTIA_CHECK(false);
    }
}

ASTIA_TEST(ps2_controller_scan_interleave_preserves_self_test_failure) {
    astia::test::fake_8042 fake;

    fake.self_test_reply                     = 0xFC;
    fake.interleave_scan_on_controller_reply = true;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 256);

    ASTIA_CHECK(controller.initialize() == ps2_error::self_test);
}

ASTIA_TEST(ps2_controller_enable_scanning_requires_ack) {
    astia::test::fake_8042 fake;

    fake.no_ack = true;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 32);

    ASTIA_CHECK(controller.enable_scanning() == ps2_error::timeout);
    ASTIA_CHECK(fake.keyboard_commands.size() == 1);
    ASTIA_CHECK(fake.keyboard_commands[0] == 0xF4);
}

ASTIA_TEST(ps2_controller_retries_resend) {
    astia::test::fake_8042 fake;

    fake.resend_count = 2;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 64);

    ASTIA_CHECK(controller.keyboard_command(0xF4) == ps2_error::none);
    ASTIA_CHECK(fake.keyboard_commands.size() == 3);
    ASTIA_CHECK(fake.keyboard_commands[0] == 0xF4);
    ASTIA_CHECK(fake.keyboard_commands[1] == 0xF4);
    ASTIA_CHECK(fake.keyboard_commands[2] == 0xF4);
}

ASTIA_TEST(ps2_controller_resend_budget_is_bounded) {
    astia::test::fake_8042 fake;

    fake.resend_count = 4;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 64);

    ASTIA_CHECK(controller.keyboard_command(0xF4) == ps2_error::protocol);
    ASTIA_CHECK(fake.keyboard_commands.size() == 3);
}

ASTIA_TEST(ps2_controller_reset_requires_ack_then_bat) {
    astia::test::fake_8042 fake;
    astia::dev::pio_bus    bus{};
    ps2_port_window        window = window_for(fake, bus);
    ps2_controller         controller(window, 64);

    ASTIA_CHECK(controller.reset_keyboard() == ps2_error::none);
    ASTIA_CHECK(fake.keyboard_commands.size() == 1);
    ASTIA_CHECK(fake.keyboard_commands[0] == 0xFF);
}

ASTIA_TEST(ps2_controller_reset_accepts_bat_before_ack) {
    astia::test::fake_8042 fake;

    fake.reset_bat_first = true;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 64);

    ASTIA_CHECK(controller.reset_keyboard() == ps2_error::none);
    ASTIA_CHECK(fake.keyboard_commands.size() == 1);
}

ASTIA_TEST(ps2_controller_reset_bat_error_fails) {
    astia::test::fake_8042 fake;

    fake.reset_bat = 0xFC;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 64);

    ASTIA_CHECK(controller.reset_keyboard() == ps2_error::self_test);
}

ASTIA_TEST(ps2_controller_reset_without_ack_times_out) {
    astia::test::fake_8042 fake;

    fake.reset_sends_ack = false;
    fake.reset_bat       = 0xAA;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 16);

    ASTIA_CHECK(controller.reset_keyboard() == ps2_error::timeout);
}

ASTIA_TEST(ps2_controller_ignores_scan_codes_before_ack) {
    astia::test::fake_8042 fake;

    fake.queue_scan_before_ack = true;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 64);

    ASTIA_CHECK(controller.keyboard_command(0xF4) == ps2_error::none);
    ASTIA_CHECK(fake.keyboard_commands.size() == 1);
    ASTIA_CHECK(fake.output.empty());
}

ASTIA_TEST(ps2_controller_input_wait_is_bounded) {
    astia::test::fake_8042 fake;

    fake.input_full_forever = true;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 32);

    ASTIA_CHECK(controller.initialize() == ps2_error::timeout);
    ASTIA_CHECK(fake.status_reads <= 33);
}

ASTIA_TEST(ps2_controller_output_wait_is_bounded) {
    astia::test::fake_8042 fake;

    fake.output_empty_forever = true;

    astia::dev::pio_bus bus{};
    ps2_port_window     window = window_for(fake, bus);
    ps2_controller      controller(window, 32);

    ASTIA_CHECK(controller.initialize() == ps2_error::timeout);
    ASTIA_CHECK(fake.status_reads <= 40);
}

ASTIA_TEST(ps2_controller_read_scan_code_is_nonblocking_and_filtered) {
    astia::test::fake_8042 fake;
    astia::dev::pio_bus    bus{};
    ps2_port_window        window = window_for(fake, bus);
    ps2_controller         controller(window, 64);

    astia::u8 byte = 0;

    ASTIA_CHECK(!controller.read_scan_code(byte));

    fake.queue_keyboard_with_flags(0x1C, 0x20);
    fake.queue_keyboard(0xFA);
    fake.queue_keyboard_with_flags(0x1C, 0x40);
    fake.queue_keyboard_with_flags(0x1C, 0x80);
    fake.queue_keyboard(0x32);
    ASTIA_CHECK(controller.read_scan_code(byte));
    ASTIA_CHECK(byte == 0x32);
    ASTIA_CHECK(!controller.read_scan_code(byte));
}

ASTIA_TEST(ps2_controller_inject_output_uses_0xd2_then_data) {
    astia::test::fake_8042 fake;
    astia::dev::pio_bus    bus{};
    ps2_port_window        window = window_for(fake, bus);
    ps2_controller         controller(window, 64);

    ASTIA_CHECK(controller.inject_output(0x4B) == ps2_error::none);
    ASTIA_CHECK(fake.operations.size() == 2);
    ASTIA_CHECK(operation_at(fake.operations, 0, true, 0xD2));
    ASTIA_CHECK(operation_at(fake.operations, 1, false, 0x4B));
    ASTIA_CHECK(fake.output.size() == 1 && fake.output.front() == 0x4B);
}

ASTIA_TEST(ps2_port_window_rejects_every_other_port) {
    astia::test::fake_8042 fake;
    astia::dev::pio_bus    bus{};
    ps2_port_window        window = window_for(fake, bus);

    astia::u8 value = 0;

    ASTIA_CHECK(window.valid());
    ASTIA_CHECK(window.read8(0x60, value));
    ASTIA_CHECK(window.read8(0x64, value));
    ASTIA_CHECK(!window.read8(0x61, value));
    ASTIA_CHECK(!window.read8(0x00, value));
    ASTIA_CHECK(!window.read8(0xFFFF, value));
    ASTIA_CHECK(window.write8(0x60, 0x11));
    ASTIA_CHECK(window.write8(0x64, 0x12));
    ASTIA_CHECK(!window.write8(0x61, 0x13));
    ASTIA_CHECK(!window.write8(0x65, 0x14));
    ASTIA_CHECK(fake.foreign_reads == 0);
    ASTIA_CHECK(fake.foreign_writes == 0);
}

ASTIA_TEST(ps2_port_window_without_bus_is_inert) {
    ps2_port_window window;
    astia::u8       value = 0;

    ASTIA_CHECK(!window.valid());
    ASTIA_CHECK(!window.read8(0x60, value));
    ASTIA_CHECK(!window.write8(0x64, 0));
}
