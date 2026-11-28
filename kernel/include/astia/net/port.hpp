#pragma once

#include <astia/common.hpp>
#include <astia/core/endian.hpp>

namespace astia::net {
    /// Transport-layer port in network byte order.
    struct port {
        core::be<u16> storage{};

        port() = default;

        explicit port(u16 value) : storage(value) {}

        u16 value() const {
            return storage.get();
        }

        operator u16() const {
            return value();
        }

        port& operator=(u16 value) {
            storage = value;

            return *this;
        }
    };

    static_assert(sizeof(port) == 2);
} // namespace astia::net
