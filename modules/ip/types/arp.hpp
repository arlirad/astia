#pragma once

#include <astia/common.hpp>

namespace astia::ip {
    /// ARP operation codes (RFC 826, RFC 5227).  Hardware types are the link's
    /// `net::hardware_type` (RFC 826 HTYPE), so ARP adds no duplicate enum.
    enum class arp_operation : u16 {
        request         = 1,
        reply           = 2,
        request_reverse = 3,
        reply_reverse   = 4,
        inverse_request = 8,
        inverse_reply   = 9,
    };
} // namespace astia::ip
