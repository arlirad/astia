#include "../xhci.hpp"

#include <cassert>
#include <cstdint>

using namespace astia::usb;
using namespace astia::usb::xhci;

namespace {
    constexpr uint8_t device_descriptor_bytes[] = {
        18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x34, 0x12, 0x78, 0x56, 0, 1, 1, 2, 3, 1};

    constexpr uint8_t configuration_header_bytes[] = {9, 2, 32, 0, 1, 1, 0, 0x80, 25};

    constexpr uint8_t configuration_bytes[] = {
        9, 2, 32, 0, 1,    1, 0, 0x80, 25, 9, 4, 0,    0, 2,  3, 1,
        1, 0, 7,  5, 0x81, 3, 8, 0,    10, 7, 5, 0x02, 2, 64, 0, 0,
    };

    void copy(const uint8_t* source, unsigned length, uint8_t* destination,
              uint16_t capacity) {
        for (unsigned i = 0; i < length && i < capacity; ++i) {
            destination[i] = source[i];
        }
    }

    struct fake_transport final : enumeration_transport {
        unsigned enable_count    = 0;
        unsigned address_count   = 0;
        unsigned transfer_count  = 0;
        unsigned disable_count   = 0;
        unsigned fail_transfer   = 0;
        uint8_t  active_slot     = 0;
        bool     fail_address    = false;
        bool     fail_descriptor = false;

        unsigned slots_in_use() const {
            return active_slot == 0 ? 0 : 1;
        }

        bool reset_port(uint8_t port, usb_speed& speed) override {
            if (port != 1)
                return false;

            speed = usb_speed::full;

            return true;
        }

        bool enable_slot(uint8_t& slot) override {
            ++enable_count;

            if (active_slot != 0)
                return false;

            active_slot = 1;
            slot        = active_slot;

            return true;
        }

        bool address_device(uint8_t slot, bool block_address, uint8_t port,
                            usb_speed speed, uint16_t max_packet_size) override {
            ++address_count;

            if (fail_address || slot != active_slot || port != 1 ||
                speed != usb_speed::full)
                return false;

            if (block_address)
                return max_packet_size == 8;

            return max_packet_size == 64;
        }

        int control_transfer(uint8_t slot, const setup_packet& setup, uint16_t,
                             uint8_t* buffer, uint16_t capacity) override {
            ++transfer_count;

            if (fail_transfer == transfer_count)
                return -32;

            if (slot != active_slot || buffer == nullptr || capacity == 0)
                return -19;

            if (setup.request == request_get_descriptor) {
                if (setup.value == static_cast<uint16_t>(descriptor_device << 8)) {
                    if (setup.length > sizeof device_descriptor_bytes)
                        return -5;

                    copy(device_descriptor_bytes, setup.length, buffer, capacity);

                    if (fail_descriptor && setup.length == 18)
                        buffer[1] = 0xff;

                    return setup.length;
                }

                if (setup.value == static_cast<uint16_t>(descriptor_configuration << 8)) {
                    const uint8_t* source = setup.length == 9 ? configuration_header_bytes
                                                              : configuration_bytes;

                    const unsigned length = setup.length;

                    if (length > sizeof configuration_bytes)
                        return -5;

                    copy(source, length, buffer, capacity);

                    return static_cast<int>(length);
                }

                return -5;
            }

            if (setup.request == request_set_configuration && setup.length == 0)
                return 0;

            return -5;
        }

        bool release_slot(uint8_t slot) override {
            ++disable_count;

            if (slot == 0 || slot != active_slot)
                return false;

            active_slot = 0;

            return true;
        }
    };

    void test_success_keeps_slot() {
        fake_transport     transport;
        device_model       model{};
        enumeration_engine engine(transport);

        assert(engine.run(1, 3, model));
        assert(transport.enable_count == 1);
        assert(transport.address_count == 2);
        assert(transport.transfer_count == 5);
        assert(transport.disable_count == 0);
        assert(transport.slots_in_use() == 1);
        assert(model.present);
        assert(model.address == 3);
        assert(model.descriptor.vendor_id == 0x1234);
        assert(model.configuration_count == 1);
    }

    void test_address_device_failure_disables_slot() {
        fake_transport     transport;
        device_model       model{};
        enumeration_engine engine(transport);

        transport.fail_address = true;

        assert(!engine.run(1, 4, model));
        assert(transport.enable_count == 1);
        assert(transport.address_count == 1);
        assert(transport.transfer_count == 0);
        assert(transport.disable_count == 1);
        assert(transport.slots_in_use() == 0);

        transport.fail_address = false;

        assert(engine.run(1, 5, model));
        assert(transport.enable_count == 2);
        assert(transport.disable_count == 1);
        assert(transport.slots_in_use() == 1);
        assert(model.address == 5);
    }

    void test_descriptor_transfer_failure_disables_slot(unsigned failing_transfer) {
        fake_transport     transport;
        device_model       model{};
        enumeration_engine engine(transport);

        transport.fail_transfer = failing_transfer;

        assert(!engine.run(1, 6, model));
        assert(transport.enable_count == 1);
        assert(transport.disable_count == 1);
        assert(transport.slots_in_use() == 0);

        transport.fail_transfer = 0;

        assert(engine.run(1, 7, model));
        assert(transport.enable_count == 2);
        assert(transport.disable_count == 1);
        assert(transport.slots_in_use() == 1);
        assert(model.address == 7);
    }

    void test_descriptor_parse_failure_disables_slot() {
        fake_transport     transport;
        device_model       model{};
        enumeration_engine engine(transport);

        transport.fail_descriptor = true;

        assert(!engine.run(1, 8, model));
        assert(transport.enable_count == 1);
        assert(transport.address_count == 1);
        assert(transport.disable_count == 1);
        assert(transport.slots_in_use() == 0);

        transport.fail_descriptor = false;

        assert(engine.run(1, 9, model));
        assert(transport.disable_count == 1);
        assert(transport.slots_in_use() == 1);
        assert(model.address == 9);
    }

    void test_control_ring_reset_releases_transfer() {
        trb                            ring_memory[64]{};
        control_transfer_engine<1, 64> engine;

        assert(engine.initialize_ring(ring_memory, 0x1000));

        const setup_packet setup = get_descriptor_request(descriptor_device, 0, 18);
        transfer_handle    handle{};
        uint8_t            buffer[18]{};

        assert(engine.submit(setup, buffer, 0x2000, 18, 8, handle));
        assert(engine.ring().busy());

        engine.reset();

        assert(!engine.ring().busy());
        assert(engine.state(handle) == transfer_state::free);
        assert(((ring_memory[63].control.get() >> 10) & 0x3fu) ==
               static_cast<uint32_t>(trb_type::link));

        transfer_handle reused{};

        assert(engine.submit(setup, buffer, 0x2000, 18, 8, reused));
        assert(reused.slot == handle.slot);
        assert(reused.generation != handle.generation);
        assert(engine.ring().busy());
    }
}

int main() {
    test_success_keeps_slot();
    test_address_device_failure_disables_slot();
    test_descriptor_transfer_failure_disables_slot(2);
    test_descriptor_transfer_failure_disables_slot(3);
    test_descriptor_transfer_failure_disables_slot(4);
    test_descriptor_parse_failure_disables_slot();
    test_control_ring_reset_releases_transfer();

    return 0;
}
