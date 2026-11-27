#pragma once

#include <astia/common.hpp>
#include <astia/mapping.hpp>

namespace astia::net {
    enum class link_kind : u8 {
        unspecified,
        ethernet,
        loopback,
        point_to_point,
        tunnel,
    };

    /// Address families shared by link-layer and protocol addresses.  The
    /// values match the userspace ABI AF_* constants; keep <sys/socket.h> and
    /// astia/api/aio.h in sync.
    enum class address_family : u16 {
        unspecified = 0,
        local       = 1, // AF_UNIX / AF_LOCAL
        inet        = 2,
        ax25        = 3,
        ipx         = 4,
        appletalk   = 5,
        netrom      = 6,
        bridge      = 7,
        atmpvc      = 8,
        x25         = 9,
        inet6       = 10,
        rose        = 11,
        decnet      = 12,
        netbeui     = 13,
        security    = 14,
        key         = 15,
        netlink     = 16,
        packet      = 17,
        ash         = 18,
        econet      = 19,
        atmsvc      = 20,
        rds         = 21,
        sna         = 22,
        irda        = 23,
        pppox       = 24,
        wanpipe     = 25,
        llc         = 26,
        ib          = 27,
        mpls        = 28,
        can         = 29,
        tipc        = 30,
        bluetooth   = 31,
        iucv        = 32,
        rxrpc       = 33,
        isdn        = 34,
        phonet      = 35,
        ieee802154  = 36,
        caif        = 37,
        alg         = 38,
        nfc         = 39,
        vsock       = 40,
        kcm         = 41,
        qipcrtr     = 42,
        smc         = 43,
        xdp         = 44,
        mctp        = 45,
    };

    static_assert(static_cast<u16>(address_family::mctp) == 45);

    /// Bounded, tagged address: sockaddr_storage-like storage without the C
    /// ABI details.  Enough for a MAC, IPv6, or a 20-byte Infiniband address.
    struct link_address {
        static constexpr u32 max_length = 32;

        address_family family = address_family::unspecified;
        u8             length = 0;
        u8             bytes[max_length]{};

        constexpr bool valid() const {
            return family != address_family::unspecified && length != 0 &&
                   length <= max_length;
        }

        constexpr bool operator==(const link_address& other) const {
            if (!valid() || !other.valid() || family != other.family ||
                length != other.length)
                return false;

            for (u32 i = 0; i < length; ++i) {
                if (bytes[i] != other.bytes[i])
                    return false;
            }

            return true;
        }

        constexpr bool operator!=(const link_address& other) const {
            return !(*this == other);
        }

        static constexpr link_address from_address(address_family family, const u8* value,
                                                   u8 size) {
            link_address result{};

            if (family == address_family::unspecified || value == nullptr || size == 0 ||
                size > max_length)
                return result;

            result.family = family;
            result.length = size;

            for (u8 i = 0; i < size; ++i) {
                result.bytes[i] = value[i];
            }

            return result;
        }

        static constexpr link_address from_packet(const u8* value, u8 size) {
            return from_address(address_family::packet, value, size);
        }
    };
} // namespace astia::net

namespace astia {
    template <>
    struct mapper<net::address_family, u32> {
        static optional<net::address_family> from(u32 raw) {
            if (raw > static_cast<u32>(net::address_family::mctp))
                return optional<net::address_family>(error_t::einval);

            return optional<net::address_family>(static_cast<net::address_family>(raw));
        }

        static u32 to(net::address_family value) {
            return static_cast<u32>(value);
        }
    };

    template <>
    struct mapper<net::address_family, u16> {
        static optional<net::address_family> from(u16 raw) {
            return mapper<net::address_family, u32>::from(raw);
        }

        static u16 to(net::address_family value) {
            return static_cast<u16>(value);
        }
    };
} // namespace astia
