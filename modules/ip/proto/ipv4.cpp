#include "ipv4.hpp"

#include <astia/net/checksum.hpp>

#include "../headers/ipv4.hpp"
#include "mapping.hpp"

namespace astia::ip {
    net::protocol_id ipv4_protocol::id() const {
        return net::protocol_id::ipv4;
    }

    void ipv4_protocol::receive(const net::protocol_packet& packet) {
        if (packet.egress == nullptr || packet.payload.length < sizeof(ipv4_header))
            return;

        const ipv4_header header = packet.payload.slice<ipv4_header>();

        const u8  version       = static_cast<u8>(header.version_ihl >> 4);
        const usz header_length = static_cast<usz>(header.version_ihl & 0x0f) * 4;
        const usz total_length  = header.total_length.value();

        if (version != 4 || header_length < sizeof(ipv4_header) ||
            total_length < header_length)
            return;

        // Fragments need reassembly, which this first implementation omits.
        if ((static_cast<u16>(header.flags_fragment) & 0x3fffu) != 0)
            return;

        if (packet.payload.length < total_length)
            return;

        if (net::internet_checksum_value(packet.payload, header_length) != 0)
            return;

        const span<const u8> destination(header.destination.bytes(), ipv4_address::size);

        if (!packet.egress->contains_address(net::protocol_id::ipv4, destination))
            return;

        const auto protocol =
            mapper<net::protocol_id, ip_protocol>::from(header.protocol.get());

        if (!protocol.has_value)
            return;

        net::protocol_packet next{};

        next.egress      = packet.egress;
        next.link_source = packet.link_source;
        next.source      = net::link_address::from_address(
            net::address_family::inet, header.source.bytes(), ipv4_address::size);
        next.destination = net::link_address::from_address(
            net::address_family::inet, header.destination.bytes(), ipv4_address::size);
        next.payload =
            packet.payload.slice(static_cast<u32>(header_length),
                                 static_cast<u32>(total_length - header_length));
        next.depth = packet.depth + 1;

        net::rx(protocol.value, next);
    }

    core::task<void> ipv4_protocol::send(const net::protocol_tx& request) {
        if (request.token.canceled())
            co_return error_t::ecanceled;

        if (request.egress == nullptr || request.builder == nullptr ||
            request.source.family != net::address_family::inet ||
            request.source.length != ipv4_address::size ||
            request.destination.family != net::address_family::inet ||
            request.destination.length != ipv4_address::size)
            co_return error_t::einval;

        const auto protocol = mapper<ip_protocol, net::protocol_id>::from(request.upper);

        if (!protocol.has_value)
            co_return error_t::einval;

        u8* header_bytes = request.builder->prepend(sizeof(ipv4_header));
        if (header_bytes == nullptr)
            co_return error_t::e2big;

        auto* header = reinterpret_cast<ipv4_header*>(header_bytes);

        *header                = {};
        header->version_ihl    = 0x45;
        header->total_length   = static_cast<u16>(request.builder->length());
        header->ttl            = 64;
        header->protocol       = protocol.value;
        header->source         = ipv4_address::from_bytes(request.source.bytes);
        header->destination    = ipv4_address::from_bytes(request.destination.bytes);
        header->checksum_value = 0;
        header->checksum_value =
            net::internet_checksum_value(header_bytes, sizeof *header);

        co_return co_await request.egress->send(request.link_destination,
                                                net::protocol_id::ipv4,
                                                request.builder->data(), request.token);
    }
} // namespace astia::ip
