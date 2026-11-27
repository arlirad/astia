#pragma once

#include <astia/common.hpp>

namespace astia::ip {
    /// IANA IP protocol numbers carried in the IPv4 header.
    enum class ip_protocol : u8 {
        hopopt = 0,
        icmp   = 1,
        igmp   = 2,
        ipv4   = 4,
        tcp    = 6,
        udp    = 17,
        dccp   = 33,
        ipv6   = 41,
        gre    = 47,
        esp    = 50,
        ah     = 51,
        icmpv6 = 58,
        sctp   = 132,
    };
} // namespace astia::ip
