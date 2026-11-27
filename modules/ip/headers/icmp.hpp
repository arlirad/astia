#pragma once

#include <astia/core/endian.hpp>
#include <astia/net/checksum.hpp>
#include <astia/net/header.hpp>
#include <astia/net/types.hpp>

namespace astia::ip {
    struct icmp_header final : net::header {
        u8                     type{};
        u8                     code{};
        net::internet_checksum checksum_value{};
        core::be<u32>          rest{};
    };

    static_assert(sizeof(icmp_header) == 8);
} // namespace astia::ip
