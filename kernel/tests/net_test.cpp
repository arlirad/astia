#include <astia/net.hpp>

#include <cassert>
#include <atomic>
#include <thread>

namespace {

struct tracker {
    int recycled = 0;
    int deferred = 0;
    int reclaimed = 0;
    int last_status = 0;
};

void recycle(void* context, int status) {
    auto& value = *static_cast<tracker*>(context);
    ++value.recycled;
    value.last_status = status;
}

uint8_t defer_rx(void* context, const astia::net::packet_view*) {
    auto& value = *static_cast<tracker*>(context);
    ++value.deferred;
    return static_cast<uint8_t>(astia::net::rx_action::defer);
}

void deliver_rx(void* context, const astia::net::packet_view*) {
    ++static_cast<tracker*>(context)->deferred;
}

void reclaim_tx(void* context, const astia::net::tx_request*, int status) {
    auto& value = *static_cast<tracker*>(context);
    ++value.reclaimed;
    value.last_status = status;
}

struct concurrent_tracker {
    std::atomic<unsigned> reclaimed{0};
};

void reclaim_concurrent(void* context, const astia::net::tx_request*, int) {
    static_cast<concurrent_tracker*>(context)->reclaimed.fetch_add(1,
                                                                    std::memory_order_relaxed);
}

astia::net::packet_view packet(tracker& value) {
    static const uint8_t bytes[] = {1, 2, 3, 4};
    astia::net::packet_view result{};
    result.first = {bytes, sizeof(bytes)};
    result.length = sizeof(bytes);
    result.wire_length = sizeof(bytes);
    result.owner = &value;
    result.recycle = recycle;
    return result;
}

} // namespace

int main() {
    astia::net::bounded_spsc_queue<unsigned, 4> spsc;
    unsigned value = 0;
    for (unsigned i = 0; i < 4; ++i)
        assert(spsc.push(i));
    assert(!spsc.push(4));
    for (unsigned i = 0; i < 4; ++i) {
        assert(spsc.pop(value));
        assert(value == i);
    }
    assert(!spsc.pop(value));
    assert(spsc.push(5));
    assert(spsc.pop(value) && value == 5);

    astia::net::bounded_mpsc_queue<unsigned, 4> mpsc;
    for (unsigned i = 0; i < 4; ++i)
        assert(mpsc.push(i + 10));
    assert(!mpsc.push(14));
    for (unsigned i = 0; i < 4; ++i) {
        assert(mpsc.pop(value));
        assert(value == i + 10);
    }

    tracker rx{};
    astia::net::rx_path path(defer_rx, deliver_rx, &rx);
    auto first = packet(rx);
    assert(path.dispatch_irq(first) == astia::net::rx_action::defer);
    assert(path.dispatch_irq(packet(rx)) == astia::net::rx_action::defer);
    assert(path.dispatch_irq(packet(rx)) == astia::net::rx_action::drop);
    assert(path.deferred_full());
    assert(rx.deferred == 3);
    assert(path.poll() == 2);
    assert(rx.recycled == 3);
    assert(rx.last_status == 0);

    astia::net::tx_path<2, 2> tx(&rx);
    astia::net::tx_request request{};
    uint8_t data = 0;
    request.data = &data;
    request.length = 1;
    request.cookie = &rx;
    request.reclaim = reclaim_tx;
    assert(tx.submit(request) == 0);
    assert(tx.submit(request) == 0);
    assert(tx.submit(request) == -EAGAIN);
    astia::net::tx_request active{};
    assert(tx.dequeue(active));
    assert(tx.complete(active, 0));
    assert(tx.poll() == 1);
    assert(rx.reclaimed == 1 && rx.last_status == 0);
    assert(tx.cancel(request));
    assert(rx.reclaimed == 2 && rx.last_status == -ECANCELED);
    tx.reset();
    assert(tx.generation() == 2);
    assert(!tx.complete(active, 0));

    astia::net::netdev device("hostnet", &rx);
    device.set_generation(7);
    assert(device.generation() == 7);
    assert(device.tx().generation() == 7);
    astia::net::tx_request public_request{};
    public_request.data = &data;
    public_request.length = 1;
    public_request.reclaim = reclaim_tx;
    assert(device.tx().submit(public_request) == 0);
    assert(device.tx().dequeue(active));
    assert(device.tx().complete(active, 0));
    assert(device.tx().poll() == 1);
    assert(rx.reclaimed == 3 && rx.last_status == 0);
    astia::net::registry registry;
    astia::net::netdev malformed(nullptr);
    assert(!malformed.valid());
    assert(!registry.register_device(malformed));
    assert(registry.register_device(device));
    assert(registry.count() == 1);
    assert(registry.unregister_device(device));
    assert(registry.count() == 0);

    {
        astia::net::netdev_handle handle(registry, device);
        assert(handle);
        assert(handle.get() == &device);
        assert(registry.count() == 1);
    }
    assert(registry.count() == 0);

    concurrent_tracker concurrent{};
    astia::net::tx_path<32, 32> concurrent_path(&concurrent);
    std::atomic<bool> producer_done{false};
    std::atomic<unsigned> accepted{0};
    std::thread producer([&] {
        uint8_t byte = 0;
        for (unsigned i = 0; i < 512; ++i) {
            astia::net::tx_request item{};
            item.data = &byte;
            item.length = 1;
            item.reclaim = reclaim_concurrent;
            while (concurrent_path.submit(item) == -EAGAIN)
                std::this_thread::yield();
            accepted.fetch_add(1, std::memory_order_relaxed);
        }
        producer_done.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        unsigned consumed = 0;
        astia::net::tx_request item{};
        while (!producer_done.load(std::memory_order_acquire) ||
               consumed != accepted.load(std::memory_order_acquire)) {
            if (!concurrent_path.dequeue(item)) {
                std::this_thread::yield();
                continue;
            }
            while (!concurrent_path.complete(item, 0))
                std::this_thread::yield();
            ++consumed;
            concurrent_path.poll();
        }
        concurrent_path.poll();
    });
    producer.join();
    consumer.join();
    assert(concurrent.reclaimed.load(std::memory_order_relaxed) == accepted.load());

    concurrent_tracker reset_tracker{};
    astia::net::tx_path<32, 8> reset_path(&reset_tracker);
    std::atomic<bool> reset_producer_done{false};
    std::atomic<unsigned> reset_accepted{0};
    std::thread reset_producer([&] {
        uint8_t byte = 0;
        for (unsigned i = 0; i < 512; ++i) {
            astia::net::tx_request item{};
            item.data = &byte;
            item.length = 1;
            item.reclaim = reclaim_concurrent;
            int result = 0;
            while ((result = reset_path.submit(item)) == -EAGAIN)
                std::this_thread::yield();
            if (result == 0)
                reset_accepted.fetch_add(1, std::memory_order_relaxed);
        }
        reset_producer_done.store(true, std::memory_order_release);
    });
    std::thread resetter([&] {
        for (unsigned i = 0; i < 32; ++i) {
            reset_path.reset();
            std::this_thread::yield();
        }
    });
    reset_producer.join();
    resetter.join();
    reset_path.reset();
    assert(reset_producer_done.load(std::memory_order_acquire));
    assert(reset_tracker.reclaimed.load(std::memory_order_relaxed) ==
           reset_accepted.load(std::memory_order_relaxed));
}
