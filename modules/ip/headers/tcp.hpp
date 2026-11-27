#pragma once

#include <astia/core/endian.hpp>
#include <astia/net/checksum.hpp>
#include <astia/net/header.hpp>
#include <astia/net/types.hpp>

#include "../types/port.hpp"

namespace astia::ip {
    struct tcp_header final : net::header {
        port                   source_port{};
        port                   destination_port{};
        core::be<u32>          sequence{};
        core::be<u32>          acknowledgement{};
        core::be<u16>          data_offset_flags{};
        core::be<u16>          window{};
        net::internet_checksum checksum_value{};
        core::be<u16>          urgent{};
    };

    static_assert(sizeof(tcp_header) == 20);
} // namespace astia::ip
