#pragma once

#include <stdint.h>

namespace astia_virtio_blk {

    constexpr uint32_t feature_read_only = 1u << 5;
    constexpr uint32_t feature_flush = 1u << 9;
    constexpr uint16_t descriptor_next = 1;
    constexpr uint16_t descriptor_write = 2;
    constexpr uint32_t request_in = 0;
    constexpr uint32_t request_out = 1;
    constexpr uint32_t request_flush = 4;

    inline void encode_header(uint8_t* bytes, uint32_t type, uint64_t sector) {
        for (unsigned i = 0; i < 4; ++i)
            bytes[i] = static_cast<uint8_t>(type >> (i * 8));
        for (unsigned i = 0; i < 4; ++i)
            bytes[4 + i] = 0;
        for (unsigned i = 0; i < 8; ++i)
            bytes[8 + i] = static_cast<uint8_t>(sector >> (i * 8));
    }

    constexpr uint16_t data_flags(bool device_writes) {
        return static_cast<uint16_t>(descriptor_next |
                                     (device_writes ? descriptor_write : 0));
    }

    constexpr bool valid_range(uint64_t capacity_sectors, uint32_t length,
                               uint64_t offset) {
        return length != 0 && (length & 511u) == 0 && (offset & 511u) == 0 &&
               offset / 512 <= capacity_sectors &&
               static_cast<uint64_t>(length / 512) <= capacity_sectors - offset / 512;
    }

    constexpr bool safe_write(uint32_t offered_features) {
        return (offered_features & feature_flush) != 0 &&
               (offered_features & feature_read_only) == 0;
    }

    constexpr bool completion_length_ok(uint32_t type, uint32_t data_length,
                                        uint32_t used_length) {
        return used_length >= (type == request_in ? data_length : 0) + 1;
    }

    enum class request_state : uint8_t { idle, submitted, canceled, completed };

}
