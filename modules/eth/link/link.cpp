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

        _dev     = mm::shweak<net::netdev>(reference);
        _mac     = mac::from_link_address(device.address());
        _address = device.address();
        _bound   = true;

        return true;
    }

    void link::detach() {
        _dev     = {};
        _mac     = {};
        _address = {};
        _bound   = false;
    }

    net::link_kind link::kind() const {
        return net::link_kind::ethernet;
    }

    const net::link_address& link::local_address() const {
        return _address;
    }

    net::mtu link::mtu() const {
        auto device = _dev.lock();

        return device ? device->mtu() : net::mtu{};
    }

    bool link::up() const {
        auto device = _dev.lock();

        return device && (device->link_flags() & net::link_up) != 0;
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

    core::task<error_t> link::send(const net::link_address& dst,
                                   net::protocol_id protocol, span<const u8> payload) {
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

        const usize total = 14 + payload.size();

        if (total > max_frame || total > static_cast<usize>(device->mtu().value) + 14)
            co_return error_t::e2big;

        tx_slot* slot = acquire_slot();
        if (slot == nullptr)
            co_return error_t::eagain;

        ethernet_header header{};

        header.destination = mac::from(dst.bytes);
        header.source      = _mac;

        header.protocol = native.value;

        const auto* header_bytes = reinterpret_cast<const u8*>(&header);

        for (u32 i = 0; i < sizeof header; ++i) {
            slot->bytes[i] = header_bytes[i];
        }

        for (u32 i = 0; i < payload.size(); ++i) {
            slot->bytes[14 + i] = payload[i];
        }

        u32 length = static_cast<u32>(total);

        while (length < min_frame) {
            slot->bytes[length++] = 0;
        }

        net::tx_request request{};

        request.data    = slot->bytes;
        request.length  = length;
        request.cookie  = slot;
        request.reclaim = &link::reclaim;

        const int result = device->tx().submit(request);

        if (result < 0) {
            release_slot(*slot);

            co_return error_from_errno(result);
        }

        co_return error_t::enone;
    }

    bool link::copy_out(const net::packet_view& packet, u8* out, u32 length) {
        const net::packet_segment* segments[2] = {&packet.first, &packet.second};

        u32 copied = 0;

        for (u32 segment_index = 0; segment_index < 2; ++segment_index) {
            const net::packet_segment& segment = *segments[segment_index];

            u32 take = segment.length;

            if (take > length - copied)
                take = length - copied;

            for (u32 i = 0; i < take; ++i) {
                out[copied + i] = segment.data[i];
            }

            copied += take;
            if (copied == length)
                return true;
        }

        return false;
    }

    void link::rx(net::packet_view packet) {
        const u32 available = packet.first.length + packet.second.length;
        if (available < 14)
            return;

        ethernet_header header{};

        if (!copy_out(packet, reinterpret_cast<u8*>(&header), sizeof header))
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

        net::dispatch_protocol(protocol.value, *this, source, packet);
    }
} // namespace astia::eth
