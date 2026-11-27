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
