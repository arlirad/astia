#include "link.hpp"

#include <astia/atomic.hpp>
#include <astia/mapping.hpp>
#include <astia/net/protocol.hpp>

#include "headers/ethernet.hpp"
#include "mapping.hpp"

namespace astia::eth {
    bool link::attach(net::netdev& device, const mm::shptr<net::netdev>& reference) {
        if (_bound || !reference)
            return false;

        for (tx_slot& slot : _tx) {
            if (slot.buffer)
                continue;

            if (!slot.buffer.allocate(1, true)) {
                for (tx_slot& allocated : _tx) {
                    allocated.buffer.release();
                }

                return false;
            }
        }

        _dev     = mm::shweak<net::netdev>(reference);
        _mac     = mac::from_link_address(device.address());
        _address = device.address();
        _bound   = true;

        return true;
    }

    void link::detach() {
        // Reclaim queued requests while this module is still mapped; their
        // callbacks point into link::reclaim.
        if (auto device = _dev.lock())
            device->tx().reset();

        for (tx_slot& slot : _tx) {
            slot.buffer.release();

            slot.owner = nullptr;
            slot.used  = false;
        }

        clear_addresses();

        _dev     = {};
        _mac     = {};
        _address = {};
        _bound   = false;
    }

    net::link_kind link::kind() const {
        return net::link_kind::ethernet;
    }

    net::hardware_type link::hardware() const {
        return net::hardware_type::ethernet;
    }

    const net::link_address& link::local_address() const {
        return _address;
    }

    link::tx_slot* link::acquire_slot() {
        for (u32 i = 0; i < tx_slots; ++i) {
            bool expected = false;

            if (atomic_ref<bool>(_tx[i].used)
                    .compare_exchange_strong(expected, true, memory_order::acq_rel,
                                             memory_order::acquire)) {
                _tx[i].owner = this;

                return &_tx[i];
            }
        }

        return {};
    }

    void link::release_slot(tx_slot& slot) {
        slot.owner = nullptr;

        atomic_ref<bool>(slot.used).store(false, memory_order::release);
    }

    void link::reclaim(void*, const net::tx_request* request, int) {
        if (request == nullptr || request->cookie == nullptr)
            return;

        auto* slot = static_cast<tx_slot*>(request->cookie);

        if (slot->owner != nullptr)
            slot->owner->release_slot(*slot);
    }

    core::task<void> link::send(const net::link_address& dst, net::protocol_id protocol,
                                span<const u8> payload, core::cltoken token) {
        if (token.canceled())
            co_return error_t::ecanceled;

        auto device = _dev.lock();
        if (!device)
            co_return error_t::enodev;

        if ((device->link_flags() & net::link_up) == 0)
            co_return error_t::enotconn;

        if (dst.family != net::address_family::packet || dst.length != 6)
            co_return error_t::einval;

        const net::link_address& local = local_address();

        if (!local.valid() || local.length != 6)
            co_return error_t::einval;

        const auto native = mapper<ethertype, net::protocol_id>::from(protocol);

        if (!native.has_value)
            co_return error_t::einval;

        const usz total = 14 + payload.size();

        if (total > max_frame || total > static_cast<usz>(device->mtu().value) + 14)
            co_return error_t::e2big;

        tx_slot* slot = acquire_slot();
        if (slot == nullptr)
            co_return error_t::eagain;

        ethernet_header header{};

        header.destination = mac::from(dst.bytes);
        header.source      = _mac;

        header.protocol = native.value;

        u8* bytes = slot->buffer.as<u8>();

        if (bytes == nullptr) {
            release_slot(*slot);

            co_return error_t::enomem;
        }

        const auto* header_bytes = reinterpret_cast<const u8*>(&header);

        for (u32 i = 0; i < sizeof header; ++i) {
            bytes[i] = header_bytes[i];
        }

        for (u32 i = 0; i < payload.size(); ++i) {
            bytes[14 + i] = payload[i];
        }

        u32 length = static_cast<u32>(total);

        while (length < min_frame) {
            bytes[length++] = 0;
        }

        net::tx_request request{};

        request.data     = bytes;
        request.physical = slot->buffer.physical();
        request.length   = length;
        request.cookie   = slot;
        request.reclaim  = &link::reclaim;

        const int result = device->tx().submit(request);

        if (result < 0) {
            release_slot(*slot);

            co_return error_from_errno(result);
        }

        co_return error_t::enone;
    }

    void link::rx(net::packet_view packet) {
        const usz available = packet.first.size() + packet.second.size();
        if (available < 14)
            return;

        ethernet_header header{};

        if (!packet.copy_to(reinterpret_cast<u8*>(&header), sizeof header))
            return;

        const net::link_address& local = local_address();

        if (local.valid() && local.length == 6) {
            const bool broadcast = header.destination.broadcast();
            const bool own       = header.destination == _mac;

            if (!broadcast && !own)
                return;
        }

        const auto protocol =
            mapper<net::protocol_id, ethertype>::from(header.protocol.get());

        if (!protocol.has_value)
            return;

        if (!packet.advance(14))
            return;

        const net::link_address source = header.source.to_link_address();

        net::rx(protocol.value, *this, source, packet);
    }
} // namespace astia::eth
