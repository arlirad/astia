#pragma once

#include <astia/api/aio.h>
#include <astia/mapping.hpp>
#include <astia/net/address.hpp>

namespace astia {
    template <>
    struct mapper<net::link_address, astia_sockaddr> {
        static optional<net::link_address> from(const astia_sockaddr& value) {
            if (value.len < 2 || value.len - 2 > sizeof(value.data))
                return optional<net::link_address>(error_t::einval);

            const u32 length = value.len - 2;

            if (length > net::link_address::max_length)
                return optional<net::link_address>(error_t::einval);

            const auto family = mapper<net::address_family, u32>::from(value.family);

            if (!family.has_value)
                return optional<net::link_address>(error_t::einval);

            net::link_address result{};

            result.family = family.value;
            result.length = static_cast<u8>(length);

            for (u32 i = 0; i < length; ++i) {
                result.bytes[i] = value.data[i];
            }

            return result;
        }
    };

    template <>
    struct mapper<astia_sockaddr, net::link_address> {
        static optional<astia_sockaddr> from(const net::link_address& value) {
            if (!value.valid())
                return optional<astia_sockaddr>(error_t::einval);

            astia_sockaddr result{};

            if (value.length > sizeof(result.data))
                return optional<astia_sockaddr>(error_t::einval);

            result.family = mapper<net::address_family, u32>::to(value.family);
            result.len    = 2 + value.length;

            for (u32 i = 0; i < value.length; ++i) {
                result.data[i] = value.bytes[i];
            }

            return result;
        }
    };
} // namespace astia
