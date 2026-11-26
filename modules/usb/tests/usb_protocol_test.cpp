#include "../hid_keyboard.hpp"
#include "../usb_protocol.hpp"
#include "../xhci.hpp"

#include <cassert>
#include <cstdint>

using namespace astia::usb;
using namespace astia::usb::hid;
using namespace astia::usb::xhci;
using namespace astia::usb::xhci;

namespace {
    bool match(const device_model&, const interface_model& interface) {
        return interface.interface_class == 8;
    }

    bool bind(const device_model&, const interface_model&, void** context) {
        *context = reinterpret_cast<void*>(static_cast<uintptr_t>(0x42));

        return true;
    }

    void unbind(void*) {}

    struct keyboard_capture {
        uint8_t  bytes[128]{};
        unsigned length = 0;

        static bool put(void* value, uint8_t byte) {
            auto& capture = *static_cast<keyboard_capture*>(value);

            if (capture.length == sizeof capture.bytes)
                return false;

            capture.bytes[capture.length++] = byte;

            return true;
        }
    };

    struct fake_transport {
        void*           report_buffer = nullptr;
        transfer_handle control{};
        transfer_handle interrupt{};
        unsigned        controls = 0;

        static bool control_submit(void* value, const setup_packet& setup, void*,
                                   uint32_t, transfer_handle&       handle) {
            auto& transport = *static_cast<fake_transport*>(value);

            assert(setup.request_type == 0x21 && setup.request == 0x0b);
            assert(setup.value == 0 && setup.length == 0);

            transport.control = {0, 1};
            handle            = transport.control;

            ++transport.controls;

            return true;
        }

        static bool interrupt_submit(void* value, uint8_t endpoint, void* buffer,
                                     uint32_t length, uint8_t interval,
                                     transfer_handle& handle) {
            auto& transport = *static_cast<fake_transport*>(value);

            assert(endpoint == 0x81 && length == 8 && interval == 10);

            transport.report_buffer = buffer;
            transport.interrupt     = {
                1, static_cast<uint16_t>(transport.interrupt.generation + 1)};

            if (transport.interrupt.generation == 0)
                transport.interrupt.generation = 1;

            handle = transport.interrupt;

            return true;
        }

        static bool cancel(void*, transfer_handle) {
            return true;
        }
    };
}

int main() {
    const setup_packet setup     = get_descriptor_request(descriptor_device, 0, 18);
    const trb          setup_trb = astia::usb::xhci::make_setup_trb(setup, true);

    assert(setup_trb.parameter.get() == 0x0012000001000680ull);
    assert(setup_trb.status.get() == 8u);
    assert(((setup_trb.control.get() >> 10) & 0x3fu) ==
           static_cast<uint32_t>(astia::usb::xhci::trb_type::setup_stage));
    assert((setup_trb.control.get() & (3u << 16)) ==
           astia::usb::xhci::trb_transfer_type_in);

    setup_packet out_setup = setup;

    out_setup.request_type = 0x00;

    const trb out_setup_trb = astia::usb::xhci::make_setup_trb(out_setup, true);

    assert(out_setup_trb.status.get() == 8u);
    assert((out_setup_trb.control.get() & (3u << 16)) ==
           astia::usb::xhci::trb_transfer_type_out);

    out_setup.length = 0;

    const trb no_data_trb = astia::usb::xhci::make_setup_trb(out_setup, true);

    assert(no_data_trb.status.get() == 8u);
    assert((no_data_trb.control.get() & (3u << 16)) ==
           astia::usb::xhci::trb_transfer_type_no_data);

    const trb data_trb = astia::usb::xhci::make_data_trb(0x2000, 18, true, 2, true);

    assert(data_trb.parameter.get() == 0x2000);
    assert((data_trb.status.get() & 0x1ffffu) == 18);
    assert(((data_trb.status.get() >> 17) & 0x1fu) == 2);
    assert((data_trb.control.get() & astia::usb::xhci::trb_direction_in) != 0);
    assert((astia::usb::xhci::make_status_trb(false, true).control.get() &
            astia::usb::xhci::trb_interrupt_on_completion) != 0);

    const trb link_trb = astia::usb::xhci::make_link_trb(0x1000, true);

    assert((link_trb.control.get() & astia::usb::xhci::trb_toggle_cycle) != 0);
    assert((link_trb.control.get() & astia::usb::xhci::trb_chain) == 0);

    assert(slot_context_speed(usb_speed::full) == 1);
    assert(slot_context_speed(usb_speed::low) == 2);
    assert(slot_context_speed(usb_speed::high) == 3);
    assert(slot_context_speed(usb_speed::super) == 4);
    assert(slot_context_speed(usb_speed::super_plus) == 5);
    assert(slot_context_speed(static_cast<usb_speed>(0xff)) == 0);
    assert(astia::usb::xhci::command_slot_bits(0) == 0);
    assert(astia::usb::xhci::command_slot_bits(1) == 0x01000000u);
    assert(astia::usb::xhci::command_slot_bits(127) == 0x7f000000u);

    trb                                ring_memory[64]{};
    astia::usb::xhci::control_ring<64> ring;

    ring.initialize(ring_memory, 0x1000);
    assert(ring.enqueue(setup, 0x2000, 18, 8));
    assert(ring.busy());
    assert(((ring.at(0).control.get() >> 10) & 0x3fu) ==
           static_cast<uint32_t>(astia::usb::xhci::trb_type::setup_stage));
    assert(((ring.at(1).control.get() >> 10) & 0x3fu) ==
           static_cast<uint32_t>(astia::usb::xhci::trb_type::data_stage));
    assert(((ring.at(2).control.get() >> 10) & 0x3fu) ==
           static_cast<uint32_t>(astia::usb::xhci::trb_type::status_stage));
    assert(!ring.enqueue(setup, 0x2000, 18, 8));
    ring.retire();

    astia::usb::xhci::control_transfer_engine<1, 64> engine;

    assert(engine.initialize_ring(ring_memory, 0x1000));

    transfer_handle control_handle{};
    uint8_t         control_buffer[18]{};

    assert(engine.submit(setup, control_buffer, 0x2000, 18, 8, control_handle));
    assert(engine.cancel(control_handle));
    assert(
        engine.complete(control_handle, astia::usb::xhci::completion_code::success, 0));
    assert(engine.state(control_handle) == transfer_state::canceled);
    assert(engine.result(control_handle) == -125);
    assert(engine.release(control_handle));
    assert(engine.submit(setup, control_buffer, 0x2000, 18, 8, control_handle));
    assert(engine.complete(control_handle, astia::usb::xhci::completion_code::stall_error,
                           0));
    assert(engine.result(control_handle) == -32);
    assert(engine.release(control_handle));

    const uint8_t     device[] = {18,   1,    0x00, 0x02, 0, 0, 0, 64, 0x34,
                                  0x12, 0x78, 0x56, 0,    1, 1, 2, 3,  1};
    device_descriptor parsed_device{};

    assert(parse_device_descriptor(device, sizeof device, parsed_device));
    assert(parsed_device.vendor_id == 0x1234);
    assert(parsed_device.product_id == 0x5678);
    assert(parsed_device.usb_version == 0x0200);

    enumeration_session session;

    assert(session.begin(1, 1, 1, usb_speed::full));

    setup_packet next_setup{};
    uint16_t     next_length = 0;

    assert(session.next(next_setup, next_length));
    assert(next_setup.length == 8 && next_length == 8);
    assert(session.complete(device, 8));
    assert(session.next(next_setup, next_length));
    assert(next_setup.length == 18 && next_length == 18);
    assert(session.complete(device, sizeof device));
    assert(session.phase() == enumeration_phase::set_address);
    assert(!session.next(next_setup, next_length));
    assert(session.complete(nullptr, 0));
    assert(session.next(next_setup, next_length));
    assert(next_setup.value == static_cast<uint16_t>(descriptor_configuration << 8));

    const uint8_t configuration_header[] = {9, 2, 32, 0, 1, 1, 0, 0x80, 25};

    assert(session.complete(configuration_header, sizeof configuration_header));
    assert(session.next(next_setup, next_length));
    assert(next_length == 32);

    const uint8_t configuration_for_session[] = {
        9, 2, 32, 0, 1,    1, 0, 0x80, 25, 9, 4, 0,    0, 2,  3, 1,
        1, 0, 7,  5, 0x81, 3, 8, 0,    10, 7, 5, 0x02, 2, 64, 0, 0,
    };

    assert(session.complete(configuration_for_session, sizeof configuration_for_session));
    assert(session.phase() == enumeration_phase::set_configuration);
    assert(session.complete(nullptr, 0));
    assert(session.phase() == enumeration_phase::complete);
    assert(!parse_device_descriptor(device, 17, parsed_device));

    const uint8_t bad_device[] = {1, 1, 0, 0, 0, 1, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

    assert(parse_device_descriptor(bad_device, sizeof bad_device, parsed_device).error ==
           parse_error::bad_length);

    const uint8_t configuration[] = {
        9, 2, 32, 0, 1,    1, 0, 0x80, 25, 9, 4, 0,    0, 2,  3, 1,
        1, 0, 7,  5, 0x81, 3, 8, 0,    10, 7, 5, 0x02, 2, 64, 0, 0,
    };
    configuration_model parsed_configuration{};

    assert(parse_configuration_descriptor(configuration, sizeof configuration,
                                          parsed_configuration));
    assert(parsed_configuration.interface_count == 1);
    assert(parsed_configuration.interfaces[0].endpoint_count == 2);
    assert(parsed_configuration.interfaces[0].endpoints[0].in);
    assert(parsed_configuration.interfaces[0].endpoints[1].type == transfer_type::bulk);
    assert(parsed_configuration.interfaces[0].endpoints[1].max_packet_size == 64);

    uint8_t malformed[sizeof configuration]{};

    for (unsigned i = 0; i < sizeof configuration; ++i) {
        malformed[i] = configuration[i];
    }

    malformed[0] = 8;

    assert(
        parse_configuration_descriptor(malformed, sizeof malformed, parsed_configuration)
            .error == parse_error::bad_length);

    malformed[0] = 9;
    malformed[2] = 33;

    assert(!parse_configuration_descriptor(malformed, sizeof malformed,
                                           parsed_configuration));

    malformed[2]  = 32;
    malformed[19] = 1;

    assert(!parse_configuration_descriptor(malformed, sizeof malformed,
                                           parsed_configuration));

    malformed[19] = 7;
    malformed[20] = 0x71;

    assert(
        parse_configuration_descriptor(malformed, sizeof malformed, parsed_configuration)
            .error == parse_error::bad_value);

    const uint8_t superspeed[] = {
        9,  2, 31, 0, 1,    1, 0, 0x80, 1, 9, 4,    0, 0, 1, 8, 6,
        80, 0, 7,  5, 0x81, 2, 0, 4,    0, 6, 0x30, 3, 0, 0, 0,
    };

    assert(parse_configuration_descriptor(superspeed, sizeof superspeed,
                                          parsed_configuration, true));
    assert(parsed_configuration.interfaces[0].endpoints[0].companion.present);

    const uint8_t missing_companion[] = {
        9, 2, 25, 0, 1, 1, 0, 0x80, 1, 9, 4, 0, 0, 1, 8, 6, 80, 0, 7, 5, 0x81, 2, 0, 4, 0,
    };

    assert(parse_configuration_descriptor(missing_companion, sizeof missing_companion,
                                          parsed_configuration, true)
               .error == parse_error::missing_companion);

    enumeration_machine machine;

    assert(machine.event(enumeration_event::attach));
    assert(machine.action() == enumeration_action::power);
    assert(machine.event(enumeration_event::power_ready));
    assert(machine.event(enumeration_event::port_reset_complete));
    assert(machine.event(enumeration_event::device_descriptor_complete));
    assert(machine.event(enumeration_event::address_complete));
    assert(machine.event(enumeration_event::configuration_complete));
    assert(machine.event(enumeration_event::set_configuration_complete));
    assert(machine.state() == astia::usb::enumeration_state::configured);
    assert(!machine.event(enumeration_event::address_complete));
    assert(machine.event(enumeration_event::detach));

    transfer_pool<1> transfers;
    transfer_handle  handle{};
    uint8_t          buffer[8]{};

    assert(transfers.submit(buffer, sizeof buffer, handle));
    assert(transfers.state(handle) == transfer_state::pending);
    assert(transfers.cancel(handle));
    assert(!transfers.cancel(handle));
    assert(!transfers.release(handle));
    assert(transfers.complete(handle, 8));
    assert(transfers.state(handle) == transfer_state::canceled);
    assert(transfers.result(handle) == -125);
    assert(transfers.release(handle));
    assert(!transfers.complete(handle, 0));

    transfer_handle second{};

    assert(transfers.submit(buffer, sizeof buffer, second));
    assert(second.generation != handle.generation);
    assert(!transfers.complete(handle, 0));
    assert(transfers.complete(second, 0));
    assert(transfers.release(second));

    driver_registry    registry;
    const driver_entry entry{"hid-test", match, bind, unbind};

    assert(registry.register_driver(entry));
    assert(!registry.register_driver(entry));

    device_model   model{};
    driver_binding binding{};

    assert(registry.bind(model, parsed_configuration.interfaces[0], binding));
    assert(binding.driver != nullptr);
    assert(binding.driver->match == match);
    assert(binding.context == reinterpret_cast<void*>(static_cast<uintptr_t>(0x42)));
    assert(registry.unregister_driver(match));

    uint8_t bar[0x800]{};

    bar[0]            = 0x20;
    bar[2]            = 0x20;
    bar[4]            = 8;
    bar[5]            = 1;
    bar[7]            = 1;
    bar[8]            = 0;
    bar[0x10]         = 1;
    bar[0x14]         = 0x40;
    bar[0x18]         = 0x80;
    bar[0x20 + 4]     = 1;
    bar[0x20 + 8]     = 1;
    bar[0x20 + 0x38]  = 8;
    bar[0x20 + 0x400] = 1;

    astia::usb::xhci::probe_result xhci_result{};

    assert(astia::usb::xhci::probe(bar, sizeof bar, xhci_result));
    assert(xhci_result.valid);
    assert(xhci_result.state ==
           astia::usb::xhci::enumeration_state::unsupported_no_transfer_engine);
    assert(xhci_result.capabilities.max_slots == 8);
    assert(xhci_result.capabilities.first_port_status == 1);
    assert(!astia::usb::xhci::probe(bar, 0x30, xhci_result));

    const uint8_t hid_configuration[] = {
        9, 2, 34,   0,    1,    1, 0, 0x80, 25, 9, 4, 0, 0,    1, 3, 1, 1,
        0, 9, 0x21, 0x11, 0x01, 0, 1, 0x22, 63, 0, 7, 5, 0x81, 3, 8, 0, 10,
    };

    assert(parse_configuration_descriptor(hid_configuration, sizeof hid_configuration,
                                          parsed_configuration));

    const interface_model& keyboard_interface = parsed_configuration.interfaces[0];

    assert(keyboard_interface.hid.present);
    assert(keyboard_interface.hid.report_length == 63);
    assert(is_boot_keyboard(keyboard_interface));
    assert(interrupt_in_endpoint(keyboard_interface) != nullptr);

    uint8_t bad_hid[sizeof hid_configuration]{};

    for (unsigned i = 0; i < sizeof bad_hid; ++i) {
        bad_hid[i] = hid_configuration[i];
    }

    bad_hid[18] = 8;

    configuration_model malformed_configuration{};

    assert(
        parse_configuration_descriptor(bad_hid, sizeof bad_hid, malformed_configuration)
            .error == parse_error::bad_length);

    keyboard_capture capture{};

    input_sink sink{&capture, keyboard_capture::put};

    boot_keyboard decoder;
    const uint8_t empty_report[8]{};

    assert(decoder.receive(empty_report, sizeof empty_report, sink) ==
           report_status::accepted);

    const uint8_t a_report[8] = {0, 0, 4, 0, 0, 0, 0, 0};

    assert(decoder.receive(a_report, sizeof a_report, sink) == report_status::accepted);
    assert(capture.length == 1 && capture.bytes[0] == 'a');
    assert(decoder.receive(a_report, sizeof a_report, sink) == report_status::accepted);
    assert(capture.length == 1);
    assert(decoder.receive(empty_report, sizeof empty_report, sink) ==
           report_status::accepted);

    const uint8_t shift_a[8] = {2, 0, 4, 0, 0, 0, 0, 0};

    assert(decoder.receive(shift_a, sizeof shift_a, sink) == report_status::accepted);
    assert(capture.length == 2 && capture.bytes[1] == 'A');
    assert(decoder.receive(empty_report, sizeof empty_report, sink) ==
           report_status::accepted);

    const uint8_t ctrl_a[8] = {1, 0, 4, 0, 0, 0, 0, 0};

    assert(decoder.receive(ctrl_a, sizeof ctrl_a, sink) == report_status::accepted);
    assert(capture.length == 3 && capture.bytes[2] == 1);

    const uint8_t rollover[8] = {0, 0, 1, 1, 1, 1, 1, 1};

    assert(decoder.receive(rollover, sizeof rollover, sink) == report_status::rollover);
    assert(!decoder.synchronized());
    assert(decoder.receive(a_report, sizeof a_report, sink) == report_status::accepted);
    assert(capture.length == 4 && capture.bytes[3] == 'a');
    assert(decoder.receive(rollover, 7, sink) == report_status::malformed);

    const uint8_t error_report[8] = {0, 0, 2, 0, 0, 0, 0, 0};

    assert(decoder.receive(error_report, sizeof error_report, sink) ==
           report_status::error);

    device_model   keyboard_device_model{};
    fake_transport fake{};

    transport keyboard_transport{&fake, fake_transport::control_submit,
                                 fake_transport::interrupt_submit,
                                 fake_transport::cancel};

    keyboard_device keyboard;

    assert(keyboard.discover(keyboard_device_model, keyboard_interface, 2,
                             keyboard_transport, sink));
    assert(keyboard.state() == device_state::discovered);
    assert(keyboard.start());
    assert(fake.controls == 1);
    assert(keyboard.control_complete(fake.control, 0));
    assert(keyboard.state() == device_state::running);

    static_cast<uint8_t*>(fake.report_buffer)[0] = 0;
    static_cast<uint8_t*>(fake.report_buffer)[1] = 0;
    static_cast<uint8_t*>(fake.report_buffer)[2] = 5;

    for (unsigned i = 3; i < 8; ++i) {
        static_cast<uint8_t*>(fake.report_buffer)[i] = 0;
    }

    const unsigned before_keyboard = capture.length;

    assert(keyboard.interrupt_complete(fake.interrupt, 8, 8));
    assert(capture.length == before_keyboard + 1 &&
           capture.bytes[before_keyboard] == 'b');
    assert(keyboard.stop());

    interrupt_transfer_engine<1> interrupt_engine;
    uint8_t                      interrupt_buffer[8]{};
    transfer_handle              interrupt_handle{};

    assert(interrupt_engine.submit(interrupt_buffer, 0x3000, sizeof interrupt_buffer, 0,
                                   interrupt_handle));
    assert(interrupt_engine.state(interrupt_handle) == transfer_state::pending);
    assert(interrupt_engine.cancel(interrupt_handle));
    assert(interrupt_engine.complete(interrupt_handle, completion_code::success, 0));
    assert(interrupt_engine.state(interrupt_handle) == transfer_state::canceled);
    assert(interrupt_engine.result(interrupt_handle) == -125);
    assert(interrupt_engine.release(interrupt_handle));
    assert(!interrupt_engine.complete(interrupt_handle, completion_code::success, 0));
}
