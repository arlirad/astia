#pragma once

#include <astia/core/endian.hpp>
#include <astia/net/checksum.hpp>
#include <astia/net/header.hpp>
#include <astia/net/packet_view.hpp>
#include <astia/net/port.hpp>
#include <astia/net/types.hpp>

namespace astia::udp {
    inline constexpr u16 header_bytes    = 8;
    inline constexpr u8  protocol_number = 17;

    /// UDP datagram header (RFC 768, 8 bytes).
    struct udp_header final : net::header {
        net::port              source_port{};
        net::port              destination_port{};
        net::length            length_value{};
        net::internet_checksum checksum_value{};
    };

    static_assert(sizeof(udp_header) == header_bytes);

    /// IPv4 pseudo-header used only for the UDP checksum (RFC 768).
    struct udp_pseudo_header final {
        u8          source[4]{};
        u8          destination[4]{};
        u8          zero     = 0;
        u8          protocol = protocol_number;
        net::length length{};
    };

    static_assert(sizeof(udp_pseudo_header) == 12);

    /// Checksum over the IPv4 pseudo-header and the whole UDP datagram.  The
    /// datagram must include the UDP header; a valid checksum field makes the
    /// result zero.
    inline u16 checksum(const udp_pseudo_header& pseudo,
                        const net::packet_view&  datagram) {
        net::internet_checksum::accumulator accumulator;

        accumulator.add(reinterpret_cast<const u8*>(&pseudo), sizeof pseudo);
        accumulator.add(datagram, datagram.length);

        return accumulator.value();
    }

    inline u16 checksum(const udp_pseudo_header& pseudo, span<const u8> datagram) {
        net::packet_view view{};

        view.first  = datagram;
        view.length = static_cast<u32>(datagram.size());

        return checksum(pseudo, view);
    }
} // namespace astia::udp
