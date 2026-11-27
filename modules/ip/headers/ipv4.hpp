#pragma once

#include <astia/core/endian.hpp>
#include <astia/net/checksum.hpp>
#include <astia/net/header.hpp>
#include <astia/net/types.hpp>

#include "../types/address.hpp"
#include "../types/protocol.hpp"

namespace astia::ip {
    /// IPv4 header without options (20 bytes).
    struct ipv4_header final : net::header {
        u8                     version_ihl{};
        u8                     dscp_ecn{};
        net::length            total_length{};
        core::be<u16>          identification{};
        core::be<u16>          flags_fragment{};
        u8                     ttl{};
        core::be<ip_protocol>  protocol{};
        net::internet_checksum checksum_value{};
        ipv4_address           source{};
        ipv4_address           destination{};
    };

    static_assert(sizeof(ipv4_header) == 20);
} // namespace astia::ip
