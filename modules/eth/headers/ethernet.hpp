#pragma once

#include <astia/core/endian.hpp>
#include <astia/net/header.hpp>

#include "../types/ethertype.hpp"
#include "../types/mac.hpp"

namespace astia::eth {
    /// Ethernet II header; addresses are typed MACs, protocol is network order.
    struct ethernet_header final : net::header {
        mac                 destination{};
        mac                 source{};
        core::be<ethertype> protocol{};
    };

    static_assert(sizeof(ethernet_header) == 14);
} // namespace astia::eth
