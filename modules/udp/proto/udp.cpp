#include "udp.hpp"

#include "../headers/udp.hpp"

namespace astia::udp {
    namespace {
        bool is_ipv4(const net::link_address& value) {
            return value.family == net::address_family::inet && value.length == 4;
        }
    } // namespace

    net::protocol_id udp_protocol::id() const {
        return net::protocol_id::udp;
    }

    void udp_protocol::receive(const net::protocol_packet& packet) {
        if (packet.egress == nullptr || packet.payload.length < header_bytes)
            return;

        if (!is_ipv4(packet.source) || !is_ipv4(packet.destination))
            return;

        const udp_header header = packet.payload.slice<udp_header>();

        const u16 length = header.length_value.value();

        // The length field covers the header and payload.  Trailing bytes
        // beyond it are padding, not part of this datagram.
        if (length < header_bytes || length > packet.payload.length)
            return;

        if (header.destination_port.value() == 0)
            return;

        if (header.checksum_value.value() != 0) {
            udp_pseudo_header pseudo{};

            for (u32 index = 0; index < 4; ++index) {
                pseudo.source[index]      = packet.source.bytes[index];
                pseudo.destination[index] = packet.destination.bytes[index];
            }

            pseudo.length = length;

            const net::packet_view datagram = packet.payload.slice(0, length);

            if (udp::checksum(pseudo, datagram) != 0)
                return;
        }

        dispatch(packet, header);
    }

    void udp_protocol::dispatch(const net::protocol_packet& packet,
                                const udp_header&           header) {
        const net::packet_view payload = packet.payload.slice(
            header_bytes, header.length_value.value() - header_bytes);

        core::spinlock_guard guard(_lock);

        for (u16 index = 0; index < max_sockets; ++index) {
            if (!_used[index])
                continue;

            socket& value = _sockets[index];

            if (value._local.port.value() != header.destination_port.value())
                continue;

            if (value._local.host.valid() && value._local.host != packet.destination)
                continue;

            if (value._connected &&
                (value._peer.host != packet.source ||
                 value._peer.port.value() != header.source_port.value()))
                continue;

            value.deliver(packet.source, header.source_port, payload);
        }
    }

    socket* udp_protocol::create() {
        core::spinlock_guard guard(_lock);

        for (u16 index = 0; index < max_sockets; ++index) {
            if (_used[index])
                continue;

            _used[index] = true;

            _sockets[index].reset(*this);

            return &_sockets[index];
        }

        return nullptr;
    }

    void udp_protocol::destroy(socket* value) {
        if (value == nullptr)
            return;

        core::spinlock_guard guard(_lock);

        if (value < &_sockets[0] || value >= &_sockets[max_sockets])
            return;

        const usz index = static_cast<usz>(value - &_sockets[0]);

        if (!_used[index])
            return;

        if (value->_bound && value->_local.port.value() != 0) {
            const u16 port = value->_local.port.value();

            _ports[port / 32] &= ~(1u << (port % 32));
        }

        value->_bound = false;
        _used[index]  = false;

        value->release();
    }

    bool udp_protocol::reserve_port(u16 requested, u16& assigned) {
        core::spinlock_guard guard(_lock);

        const auto taken = [this](u16 port) {
            return (_ports[port / 32] & (1u << (port % 32))) != 0;
        };

        if (requested != 0) {
            if (taken(requested))
                return false;

            _ports[requested / 32] |= 1u << (requested % 32);
            assigned = requested;

            return true;
        }

        const u32 span_size = static_cast<u32>(ephemeral_last) - ephemeral_first + 1;

        for (u32 attempt = 0; attempt < span_size; ++attempt) {
            const u16 port = _next_ephemeral;

            _next_ephemeral =
                port == ephemeral_last ? ephemeral_first : static_cast<u16>(port + 1);

            if (taken(port))
                continue;

            _ports[port / 32] |= 1u << (port % 32);
            assigned = port;

            return true;
        }

        return false;
    }
} // namespace astia::udp
