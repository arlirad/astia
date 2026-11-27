#pragma once

#include <astia/common.hpp>

namespace astia::acpi::detail {
    constexpr u32 name_segment(char a, char b, char c, char d) {
        return static_cast<u32>(static_cast<u8>(a)) |
               (static_cast<u32>(static_cast<u8>(b)) << 8) |
               (static_cast<u32>(static_cast<u8>(c)) << 16) |
               (static_cast<u32>(static_cast<u8>(d)) << 24);
    }

    constexpr bool lead_name_char(u8 value) {
        return (value >= 'A' && value <= 'Z') || value == '_';
    }

    constexpr bool name_char(u8 value) {
        return lead_name_char(value) || (value >= '0' && value <= '9');
    }
} // namespace astia::acpi::detail
