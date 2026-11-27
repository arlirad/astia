#pragma once

#include <astia/common.hpp>

namespace astia::net {
    /// IANA ARP hardware types (RFC 826 HTYPE).  A link reports the value for
    /// its medium so address-resolution protocols can label requests without
    /// assuming Ethernet.
    enum class hardware_type : u16 {
        unspecified      = 0,
        ethernet         = 1,
        experimental_eth = 2,
        ax25             = 3,
        ieee802          = 6,
        frame_relay      = 15,
        atm              = 16,
        ieee1394         = 24,
        infiniband       = 32,
    };
} // namespace astia::net
