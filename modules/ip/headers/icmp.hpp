#pragma once

#include <astia/core/endian.hpp>
#include <astia/net/checksum.hpp>
#include <astia/net/header.hpp>
#include <astia/net/types.hpp>

#include "../types/icmp.hpp"

namespace astia::ip {
    struct icmp_header final : net::header {
        core::be<icmp_type>    type{};
        u8                     code{};
        net::internet_checksum checksum_value{};
        core::be<u32>          rest{};
    };

    static_assert(sizeof(icmp_header) == 8);

    /// ICMP echo request/reply body: type, code, checksum, identifier,
    /// sequence, then opaque payload.
    struct icmp_echo_header final : net::header {
        core::be<icmp_type>    type{};
        u8                     code{};
        net::internet_checksum checksum_value{};
        core::be<u16>          identifier{};
        core::be<u16>          sequence{};
    };

    static_assert(sizeof(icmp_echo_header) == 8);
} // namespace astia::ip
