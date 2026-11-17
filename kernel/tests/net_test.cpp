#include <astia/net.hpp>

#include <cassert>

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

uint8_t defer_rx(void* context, const astia_module_net_packet_view*) {
    auto& value = *static_cast<tracker*>(context);
    ++value.deferred;
    return ASTIA_MODULE_NET_RX_DEFER;
}

void deliver_rx(void* context, const astia_module_net_packet_view*) {
    ++static_cast<tracker*>(context)->deferred;
}

void reclaim_tx(void* context, const astia_module_net_tx_request*, int status) {
    auto& value = *static_cast<tracker*>(context);
    ++value.reclaimed;
    value.last_status = status;
}

astia_module_net_packet_view packet(tracker& value) {
    static const uint8_t bytes[] = {1, 2, 3, 4};
    astia_module_net_packet_view result{};
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

    astia::net::tx_path<2, 2> tx;
    astia_module_net_tx_request request{};
    uint8_t data = 0;
    request.data = &data;
    request.length = 1;
    request.cookie = &rx;
    request.reclaim = reclaim_tx;
    assert(tx.submit(request) == 0);
    assert(tx.submit(request) == 0);
    assert(tx.submit(request) == -EAGAIN);
    astia_module_net_tx_request active{};
    assert(tx.dequeue(active));
    assert(tx.complete(active, 0));
    assert(tx.poll() == 1);
    assert(rx.reclaimed == 1 && rx.last_status == 0);
    tx.reset();
    assert(rx.reclaimed == 2 && rx.last_status == -ECANCELED);
    assert(tx.generation() == 2);
    assert(!tx.complete(active, 0));

    astia_module_net_device device{};
    device.name = "hostnet";
    device.context = &rx;
    device.mtu = 1500;
    astia::net::registry registry;
    assert(registry.register_device(device));
    assert(registry.count() == 1);
    assert(registry.unregister_device(device));
    assert(registry.count() == 0);
}
