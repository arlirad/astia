#pragma once

#include <astia/core/endian.hpp>
#include <astia/net/header.hpp>

#include "../../ip/types/address.hpp"
#include "../types/arp.hpp"
#include "../types/ethertype.hpp"
#include "../types/mac.hpp"

namespace astia::eth {
    /// ARP for Ethernet/IPv4 (28 bytes).  ARP lives in the Ethernet module.
    /// Multi-byte fields are network order; hardware addresses are MACs and
    /// protocol addresses are IPv4 addresses.
    struct arp_header final : net::header {
        core::be<arp_hardware>  hardware_type{};
        core::be<ethertype>     protocol_type{};
        u8                      hardware_length{};
        u8                      protocol_length{};
        core::be<arp_operation> operation{};
        mac                     sender_hardware{};
        ip::ipv4_address        sender_protocol{};
        mac                     target_hardware{};
        ip::ipv4_address        target_protocol{};
    };

    static_assert(sizeof(arp_header) == 28);
} // namespace astia::eth
