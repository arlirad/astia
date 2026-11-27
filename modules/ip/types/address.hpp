#pragma once

#include <astia/common.hpp>
#include <astia/core/endian.hpp>

namespace astia::ip {
    /// IPv4 address stored in network byte order.
    struct ipv4_address {
        static constexpr u32 size = 4;

        core::be<u32> storage{};

        ipv4_address() = default;

        explicit ipv4_address(u32 value) : storage(value) {}

        u32 value() const {
            return storage.get();
        }

        const u8* bytes() const {
            return reinterpret_cast<const u8*>(&storage);
        }

        static ipv4_address from_bytes(const u8* value) {
            ipv4_address result{};

            if (value == nullptr)
                return result;

            auto* destination = reinterpret_cast<u8*>(&result.storage);

            for (u32 index = 0; index < size; ++index) {
                destination[index] = value[index];
            }

            return result;
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
