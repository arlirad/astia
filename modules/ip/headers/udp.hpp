#pragma once

#include <astia/core/endian.hpp>
#include <astia/net/checksum.hpp>
#include <astia/net/header.hpp>
#include <astia/net/types.hpp>

#include "../types/port.hpp"

namespace astia::ip {
    struct udp_header final : net::header {
        port                   source_port{};
        port                   destination_port{};
        net::length            length_value{};
        net::internet_checksum checksum_value{};
    };

    static_assert(sizeof(udp_header) == 8);
} // namespace astia::ip
