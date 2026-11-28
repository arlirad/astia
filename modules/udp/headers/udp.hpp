#pragma once

#include <astia/core/endian.hpp>
#include <astia/net/checksum.hpp>
#include <astia/net/header.hpp>
#include <astia/net/port.hpp>
#include <astia/net/types.hpp>

namespace astia::udp {
    /// UDP datagram header (RFC 768, 8 bytes).
    struct udp_header final : net::header {
        net::port              source_port{};
        net::port              destination_port{};
        net::length            length_value{};
        net::internet_checksum checksum_value{};
    };

    static_assert(sizeof(udp_header) == 8);
} // namespace astia::udp
