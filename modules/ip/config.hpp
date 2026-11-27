#pragma once

#include "types/address.hpp"

namespace astia::ip {
    /// Static local address used until DHCP/configuration exists.  10.0.2.15
    /// is the guest address of QEMU user networking.
    inline constexpr u32 default_local_address = 0x0A00020Fu;

    inline ipv4_address local_address() {
        return ipv4_address{default_local_address};
    }
} // namespace astia::ip
