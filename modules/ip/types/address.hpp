#pragma once

#include <astia/common.hpp>
#include <astia/core/endian.hpp>

namespace astia::ip {
    /// IPv4 address stored in network byte order.
    struct ipv4_address {
        core::be<u32> storage{};

        ipv4_address() = default;

        explicit ipv4_address(u32 value) : storage(value) {}

        u32 value() const {
            return storage.get();
        }

        operator u32() const {
            return value();
        }

        ipv4_address& operator=(u32 value) {
            storage = value;

            return *this;
        }

        bool operator==(const ipv4_address& other) const {
            return value() == other.value();
        }

        bool operator!=(const ipv4_address& other) const {
            return !(*this == other);
        }
    };

    static_assert(sizeof(ipv4_address) == 4);
} // namespace astia::ip
