#pragma once

#include <astia/common.hpp>
#include <astia/net/address.hpp>

namespace astia::eth {
    /// 48-bit Ethernet hardware address.
    struct mac {
        static constexpr u32 size = 6;

        u8 bytes[size]{};

        constexpr bool operator==(const mac& other) const {
            for (u32 i = 0; i < size; ++i) {
                if (bytes[i] != other.bytes[i])
                    return false;
            }

            return true;
        }

        constexpr bool operator!=(const mac& other) const {
            return !(*this == other);
        }

        constexpr bool broadcast() const {
            for (u32 i = 0; i < size; ++i) {
                if (bytes[i] != 0xff)
                    return false;
            }

            return true;
        }

        static constexpr mac broadcast_address() {
            mac result{};

            for (u32 i = 0; i < size; ++i) {
                result.bytes[i] = 0xff;
            }

            return result;
        }

        static constexpr mac from(const u8* value) {
            mac result{};

            if (value == nullptr)
                return result;

            for (u32 i = 0; i < size; ++i) {
                result.bytes[i] = value[i];
            }

            return result;
        }

        constexpr net::link_address to_link_address() const {
            return net::link_address::from_packet(bytes, size);
        }

        static constexpr mac from_link_address(const net::link_address& value) {
            if (value.family != net::address_family::packet || value.length != size)
                return {};

            return from(value.bytes);
        }
    };

    static_assert(sizeof(mac) == 6);
} // namespace astia::eth
