#include "../usb_protocol.hpp"
#include "../xhci.hpp"

#include <cassert>
#include <cstdint>

using namespace astia::usb;

namespace {

bool match(const device_model&, const interface_model& interface) {
    return interface.interface_class == 8;
}

bool bind(const device_model&, const interface_model&, void** context) {
    *context = reinterpret_cast<void*>(static_cast<uintptr_t>(0x42));
    return true;
}

void unbind(void*) {}

}

int main() {
    const uint8_t device[] = {18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x34, 0x12,
                              0x78, 0x56, 0, 1, 1, 2, 3, 1};
    device_descriptor parsed_device{};
    assert(parse_device_descriptor(device, sizeof device, parsed_device));
    assert(parsed_device.vendor_id == 0x1234);
    assert(parsed_device.product_id == 0x5678);
    assert(parsed_device.usb_version == 0x0200);
    assert(!parse_device_descriptor(device, 17, parsed_device));
    const uint8_t bad_device[] = {1, 1, 0, 0, 0, 1, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    assert(parse_device_descriptor(bad_device, sizeof bad_device, parsed_device).error ==
           parse_error::bad_length);

    const uint8_t configuration[] = {
        9, 2, 32, 0, 1, 1, 0, 0x80, 25,
        9, 4, 0, 0, 2, 3, 1, 1, 0,
        7, 5, 0x81, 3, 8, 0, 10,
        7, 5, 0x02, 2, 64, 0, 0,
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
    for (unsigned i = 0; i < sizeof configuration; ++i)
        malformed[i] = configuration[i];
    malformed[0] = 8;
    assert(parse_configuration_descriptor(malformed, sizeof malformed, parsed_configuration).error ==
           parse_error::bad_length);
    malformed[0] = 9;
    malformed[2] = 33;
    assert(!parse_configuration_descriptor(malformed, sizeof malformed, parsed_configuration));
    malformed[2] = 32;
    malformed[19] = 1;
    assert(!parse_configuration_descriptor(malformed, sizeof malformed, parsed_configuration));
    malformed[19] = 7;
    malformed[20] = 0x71;
    assert(parse_configuration_descriptor(malformed, sizeof malformed, parsed_configuration).error ==
           parse_error::bad_value);

    const uint8_t superspeed[] = {
        9, 2, 31, 0, 1, 1, 0, 0x80, 1,
        9, 4, 0, 0, 1, 8, 6, 80, 0,
        7, 5, 0x81, 2, 0, 4, 0,
        6, 0x30, 3, 0, 0, 0,
    };
    assert(parse_configuration_descriptor(superspeed, sizeof superspeed,
                                          parsed_configuration, true));
    assert(parsed_configuration.interfaces[0].endpoints[0].companion.present);
    const uint8_t missing_companion[] = {
        9, 2, 25, 0, 1, 1, 0, 0x80, 1,
        9, 4, 0, 0, 1, 8, 6, 80, 0,
        7, 5, 0x81, 2, 0, 4, 0,
    };
    assert(parse_configuration_descriptor(missing_companion, sizeof missing_companion,
                                          parsed_configuration, true).error ==
           parse_error::missing_companion);

    enumeration_machine machine;
    assert(machine.event(enumeration_event::attach));
    assert(machine.action() == enumeration_action::power);
    assert(machine.event(enumeration_event::power_ready));
    assert(machine.event(enumeration_event::port_reset_complete));
    assert(machine.event(enumeration_event::device_descriptor_complete));
    assert(machine.event(enumeration_event::address_complete));
    assert(machine.event(enumeration_event::configuration_complete));
    assert(machine.event(enumeration_event::set_configuration_complete));
    assert(machine.state() == enumeration_state::configured);
    assert(!machine.event(enumeration_event::address_complete));
    assert(machine.event(enumeration_event::detach));

    transfer_pool<1> transfers;
    transfer_handle handle{};
    uint8_t buffer[8]{};
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

    driver_registry registry;
    const driver_entry entry{"hid-test", match, bind, unbind};
    assert(registry.register_driver(entry));
    assert(!registry.register_driver(entry));
    device_model model{};
    driver_binding binding{};
    assert(registry.bind(model, parsed_configuration.interfaces[0], binding));
    assert(binding.driver != nullptr);
    assert(binding.driver->match == match);
    assert(binding.context == reinterpret_cast<void*>(static_cast<uintptr_t>(0x42)));
    assert(registry.unregister_driver(match));

    uint8_t bar[0x800]{};
    bar[0] = 0x20;
    bar[2] = 0x20;
    bar[4] = 8;
    bar[5] = 1;
    bar[7] = 1;
    bar[8] = 0;
    bar[0x10] = 1;
    bar[0x14] = 0x40;
    bar[0x18] = 0x80;
    bar[0x20 + 4] = 1;
    bar[0x20 + 8] = 1;
    bar[0x20 + 0x38] = 8;
    bar[0x20 + 0x400] = 1;
    astia::usb::xhci::probe_result xhci_result{};
    assert(astia::usb::xhci::probe(bar, sizeof bar, xhci_result));
    assert(xhci_result.valid);
    assert(xhci_result.state ==
           astia::usb::xhci::enumeration_state::unsupported_no_transfer_engine);
    assert(xhci_result.capabilities.max_slots == 8);
    assert(xhci_result.capabilities.first_port_status == 1);
    assert(!astia::usb::xhci::probe(bar, 0x30, xhci_result));
}
