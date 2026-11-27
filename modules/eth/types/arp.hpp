#pragma once

#include <astia/common.hpp>

namespace astia::eth {
    /// IANA ARP hardware types (RFC 826 "HTYPE").
    enum class arp_hardware : u16 {
        ethernet    = 1,
        ieee802     = 6,
        frame_relay = 15,
        atm         = 16,
        ieee1394    = 24,
        infiniband  = 32,
    };

    /// ARP operation codes (RFC 826, RFC 5227).
    enum class arp_operation : u16 {
        request         = 1,
        reply           = 2,
        request_reverse = 3,
        reply_reverse   = 4,
        inverse_request = 8,
        inverse_reply   = 9,
    };
} // namespace astia::eth
