#pragma once

#include <astia/common.hpp>
#include <astia/core/endian.hpp>

namespace astia::net {
    /// Network-order wire length.
    struct length {
        core::be<u16> storage{};

        length() = default;

        explicit length(u16 value) : storage(value) {}

        u16 value() const {
            return storage.get();
        }

        operator u16() const {
            return value();
        }

        length& operator=(u16 value) {
            storage = value;

            return *this;
        }
    };

    static_assert(sizeof(length) == 2);

    /// MTU assumed before a link reports its own.
    inline constexpr u16 default_mtu = 1500;

    /// Capacity of the stack's static datagram buffers.  Larger than the
    /// default MTU so links with a bigger MTU fit without reallocation.
    inline constexpr u16 datagram_capacity = 2048;

    /// Host-order link MTU in bytes.
    struct mtu {
        u16 value = 0;

        constexpr mtu() = default;

        constexpr explicit mtu(u16 value_) : value(value_) {}

        constexpr operator u16() const {
            return value;
        }
    };
} // namespace astia::net
