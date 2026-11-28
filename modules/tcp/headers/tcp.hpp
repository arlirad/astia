#pragma once

#include <astia/core/endian.hpp>
#include <astia/net/checksum.hpp>
#include <astia/net/header.hpp>
#include <astia/net/port.hpp>
#include <astia/net/types.hpp>

namespace astia::tcp {
    /// TCP header without options (RFC 9293, 20 bytes).
    struct tcp_header final : net::header {
        net::port              source_port{};
        net::port              destination_port{};
        core::be<u32>          sequence{};
        core::be<u32>          acknowledgement{};
        core::be<u16>          data_offset_flags{};
        core::be<u16>          window{};
        net::internet_checksum checksum_value{};
        core::be<u16>          urgent{};

        u32 data_offset() const {
            return (static_cast<u16>(data_offset_flags) >> 12) * 4u;
        }
    };

    static_assert(sizeof(tcp_header) == 20);
} // namespace astia::tcp
