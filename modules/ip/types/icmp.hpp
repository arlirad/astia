#pragma once

#include <astia/common.hpp>

namespace astia::ip {
    /// ICMP message types (RFC 792 and extensions).
    enum class icmp_type : u8 {
        echo_reply              = 0,
        destination_unreachable = 3,
        source_quench           = 4,
        redirect                = 5,
        echo_request            = 8,
        router_advertisement    = 9,
        router_solicitation     = 10,
        time_exceeded           = 11,
        parameter_problem       = 12,
        timestamp_request       = 13,
        timestamp_reply         = 14,
        information_request     = 15,
        information_reply       = 16,
        address_mask_request    = 17,
        address_mask_reply      = 18,
    };
} // namespace astia::ip
