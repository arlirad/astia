#include "../rtl8139_protocol.hpp"

#include <cassert>
#include <cstdint>

using namespace astia::rtl8139;

namespace {
    void write_ring(uint8_t* ring, uint16_t offset, const uint8_t* data,
                    uint16_t length) {
        for (uint16_t index = 0; index < length; ++index) {
            ring[(static_cast<uint32_t>(offset) + index) & (rx_ring_size - 1u)] =
                data[index];
        }
    }

    void reclaim(void*, const tx_request*, int) {}

    void test_rx_ownership() {
        uint8_t storage[rx_ring_allocation]{};
        rx_ring ring;

        ring.reset(storage, static_cast<uint16_t>(rx_ring_size - 5));

        const uint8_t header[]  = {static_cast<uint8_t>(rx_status_ok), 0, 12, 0};
        const uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
        const uint8_t crc[]     = {9, 10, 11, 12};

        write_ring(storage, ring.cursor(), header, sizeof header);
        write_ring(storage, static_cast<uint16_t>(ring.cursor() + sizeof header), payload,
                   sizeof payload);
        write_ring(storage,
                   static_cast<uint16_t>(ring.cursor() + sizeof header + sizeof payload),
                   crc, sizeof crc);

        rx_packet packet{};

        assert(ring.acquire(packet) == rx_inspect_result::ready);
        assert(packet.view.segment_count() == 2);
        assert(ring.recycle(packet));
        assert(!ring.recycle(packet));
    }

    void test_tx_ownership() {
        const uint8_t payload[] = {1, 2, 3, 4};
        tx_request    request{};

        request.data     = payload;
        request.physical = 0x1000;
        request.length   = sizeof payload;
        request.reclaim  = reclaim;

        tx_ring ring;

        ring.initialize();
        assert(ring.available() == 0);
        assert(ring.claim(0, request));
        assert(!ring.claim(0, request));
        assert(ring.state(0) == tx_slot_state::device);

        tx_request completed{};

        assert(ring.complete(0, completed));
        assert(completed.data == request.data);
        assert(ring.state(0) == tx_slot_state::completion);
        assert(ring.restore(0));
        assert(ring.state(0) == tx_slot_state::device);
        assert(ring.release(0));
        assert(ring.state(0) == tx_slot_state::free);
    }
}

int main() {
    static_assert(rx_ring_pages == 3);
    static_assert(sizeof(register_offset) == sizeof(uint16_t));

    assert(pci_match(nullptr) == false);
    test_rx_ownership();
    test_tx_ownership();

    uint8_t        ring[rx_ring_allocation]{};
    const uint16_t cursor    = static_cast<uint16_t>(rx_ring_size - 5);
    const uint8_t  header[]  = {static_cast<uint8_t>(rx_status_ok), 0,
                                static_cast<uint8_t>(12), 0};
    const uint8_t  payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t  crc[]     = {9, 10, 11, 12};

    write_ring(ring, cursor, header, sizeof header);
    write_ring(ring, static_cast<uint16_t>(cursor + sizeof header), payload,
               sizeof payload);
    write_ring(ring, static_cast<uint16_t>(cursor + sizeof header + sizeof payload), crc,
               sizeof crc);

    rx_packet packet{};

    assert(inspect_rx_packet(ring, cursor, packet) == rx_inspect_result::ready);
    assert(packet.view.length == sizeof payload);
    assert(packet.view.segment_count() == 2);
    assert(packet.view.first.length == 1);
    assert(packet.view.second.length == 7);
    assert(packet.view.first.data[0] == 1);
    assert(packet.view.second.data[6] == 8);
    assert(packet.view.next_offset == 12);
    assert(capr_for_cursor(0) == rx_ring_size - 16);

    ring[cursor]                             = 0;
    ring[(cursor + 1) & (rx_ring_size - 1u)] = 0;
    ring[(cursor + 2) & (rx_ring_size - 1u)] = 0;
    ring[(cursor + 3) & (rx_ring_size - 1u)] = 0;

    assert(inspect_rx_packet(ring, cursor, packet) == rx_inspect_result::empty);

    bounded_spsc_queue<uint32_t, 4> deferred;

    assert(deferred.push(1));
    assert(deferred.push(2));
    assert(deferred.push(3));
    assert(deferred.push(4));
    assert(!deferred.push(5));

    uint32_t value = 0;

    assert(deferred.pop(value) && value == 1);
    assert(deferred.pop(value) && value == 2);
    assert(deferred.pop(value) && value == 3);
    assert(deferred.pop(value) && value == 4);
    assert(!deferred.pop(value));

    bounded_mpsc_queue<uint32_t, 4> pending;

    assert(pending.push(10));
    assert(pending.push(11));
    assert(pending.push(12));
    assert(pending.push(13));
    assert(!pending.push(14));
    assert(pending.pop(value) && value == 10);
    assert(pending.pop(value) && value == 11);
    assert(pending.pop(value) && value == 12);
    assert(pending.pop(value) && value == 13);
    assert(!pending.pop(value));

    tx_request request{};

    request.data     = payload;
    request.physical = 0x1000;
    request.length   = sizeof payload;
    request.reclaim  = reclaim;

    assert(request.length <= tx_max_frame);
    assert(tx_status_own == (1u << 13));
    assert(tx_status_ok == (1u << 15));
}
