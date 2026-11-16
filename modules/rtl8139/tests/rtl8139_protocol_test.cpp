#include "../rtl8139_protocol.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>

using namespace astia::rtl8139;

namespace {

void write_ring(uint8_t* ring, uint16_t offset, const uint8_t* data, uint16_t length) {
    for (uint16_t index = 0; index < length; ++index)
        ring[(static_cast<uint32_t>(offset) + index) & (rx_ring_size - 1u)] = data[index];
}

void reclaim(void*, const tx_request&, int) {}

void test_rx_ownership() {
    uint8_t storage[rx_ring_allocation]{};
    rx_ring ring(storage);
    const uint16_t cursor = static_cast<uint16_t>(rx_ring_size - 5);
    const uint8_t header[] = {static_cast<uint8_t>(rx_status_ok), 0, 12, 0};
    const uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t crc[] = {9, 10, 11, 12};
    write_ring(storage, cursor, header, sizeof header);
    write_ring(storage, static_cast<uint16_t>(cursor + sizeof header), payload, sizeof payload);
    write_ring(storage, static_cast<uint16_t>(cursor + sizeof header + sizeof payload), crc,
               sizeof crc);

    rx_packet packet{};
    assert(ring.cursor() == 0);
    ring.reset(storage, cursor);
    assert(ring.acquire(packet) == rx_inspect_result::ready);
    assert(packet.view.length == sizeof payload);
    assert(packet.view.segment_count() == 2);
    assert(packet.view.first.length == 1);
    assert(packet.view.second.length == 7);
    assert(packet.view.first.data[0] == 1);
    assert(packet.view.second.data[6] == 8);
    assert(ring.cursor() == cursor);

    bounded_spsc_queue<rx_packet, rx_deferred_depth> deferred;
    assert(deferred.push(packet));
    assert(ring.cursor() == cursor);
    rx_packet borrowed{};
    assert(deferred.pop(borrowed));
    assert(ring.recycle(borrowed));
    assert(ring.cursor() == packet.view.next_offset);
    assert(!ring.recycle(borrowed));
}

void test_tx_ownership() {
    const uint8_t payload[] = {1, 2, 3, 4};
    tx_request request{};
    request.data = payload;
    request.physical = 0x1000;
    request.length = sizeof payload;
    request.reclaim = reclaim;

    tx_ring ring;
    assert(ring.available() == 0);
    assert(ring.claim(0, request));
    assert(ring.state(0) == tx_slot_state::device);
    assert(ring.request(0) != nullptr);
    assert(ring.request(0)->data == request.data);
    assert(!ring.claim(0, request));

    tx_completion completion{};
    assert(ring.complete(0, 0, completion));
    assert(completion.request.data == request.data);
    assert(completion.descriptor == 0);
    assert(ring.state(0) == tx_slot_state::completion);
    assert(ring.release(0));
    assert(ring.state(0) == tx_slot_state::free);
    assert(ring.claim(0, request));
    assert(ring.complete(0, -5, completion));
    assert(ring.restore(0));
    assert(ring.state(0) == tx_slot_state::device);
}

void test_queue_wrap() {
    bounded_spsc_queue<uint32_t, 4> deferred;
    for (uint32_t round = 0; round < 64; ++round) {
        for (uint32_t value = 0; value < 4; ++value)
            assert(deferred.push(round * 4 + value));
        assert(!deferred.push(0));
        for (uint32_t value = 0; value < 4; ++value) {
            uint32_t got = 0;
            assert(deferred.pop(got));
            assert(got == round * 4 + value);
        }
    }
}

void test_mpsc_concurrency() {
    constexpr uint32_t producer_count = 4;
    constexpr uint32_t values_per_producer = 256;
    constexpr uint32_t value_count = producer_count * values_per_producer;
    bounded_mpsc_queue<uint32_t, 16> pending;
    std::array<std::atomic<uint32_t>, value_count> seen{};
    for (auto& value : seen)
        value.store(0, std::memory_order_relaxed);

    std::array<std::thread, producer_count> producers{};
    for (uint32_t producer = 0; producer < producer_count; ++producer) {
        producers[producer] = std::thread([&pending, producer] {
            for (uint32_t index = 0; index < values_per_producer; ++index) {
                const uint32_t value = producer * values_per_producer + index;
                while (!pending.push(value))
                    std::this_thread::yield();
            }
        });
    }

    uint32_t consumed = 0;
    while (consumed < value_count) {
        uint32_t value = 0;
        if (!pending.pop(value)) {
            std::this_thread::yield();
            continue;
        }
        assert(value < value_count);
        seen[value].fetch_add(1, std::memory_order_relaxed);
        ++consumed;
    }
    for (auto& producer : producers)
        producer.join();
    for (const auto& value : seen)
        assert(value.load(std::memory_order_relaxed) == 1);
}

}

int main() {
    static_assert(rx_ring_pages == 3);
    static_assert(sizeof(register_offset) == sizeof(uint16_t));
    static_assert(__atomic_always_lock_free(sizeof(uint32_t), nullptr));
    assert(__atomic_is_lock_free(sizeof(uint32_t), nullptr));
    assert(pci_match(nullptr) == false);
    assert(capr_for_cursor(0) == rx_ring_size - 16);
    assert(tx_status_own == (1u << 13));
    assert(tx_status_ok == (1u << 15));

    test_rx_ownership();
    test_tx_ownership();
    test_queue_wrap();
    test_mpsc_concurrency();
    return 0;
}
