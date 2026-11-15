#pragma once

#include "usb_protocol.hpp"

namespace astia::usb::xhci {

enum class enumeration_state : uint8_t { capabilities_only, unsupported_no_transfer_engine };

struct capability_snapshot {
    uint8_t capability_length = 0;
    uint16_t interface_version = 0;
    uint8_t max_slots = 0;
    uint16_t max_interrupters = 0;
    uint8_t max_ports = 0;
    uint16_t max_scratchpad_buffers = 0;
    bool context_size_64 = false;
    bool address_64 = false;
    uint32_t doorbell_offset = 0;
    uint32_t runtime_offset = 0;
    uint32_t operational_status = 0;
    uint32_t page_size_mask = 0;
    uint32_t configure = 0;
    uint32_t first_port_status = 0;
    bool ready = false;
    bool halted = false;
    bool page_size_4k = false;
};

struct probe_result {
    bool valid = false;
    enumeration_state state = enumeration_state::capabilities_only;
    capability_snapshot capabilities{};
};

inline bool read8(const volatile uint8_t* base, uint64_t size, uint64_t offset,
                  uint8_t& result) {
    if (base == nullptr || offset >= size)
        return false;
    result = base[offset];
    return true;
}

inline bool read16(const volatile uint8_t* base, uint64_t size, uint64_t offset,
                   uint16_t& result) {
    uint8_t bytes[2]{};
    if (offset > size || size - offset < 2 || !read8(base, size, offset, bytes[0]) ||
        !read8(base, size, offset + 1, bytes[1]))
        return false;
    result = le16(bytes);
    return true;
}

inline bool read32(const volatile uint8_t* base, uint64_t size, uint64_t offset,
                   uint32_t& result) {
    uint8_t bytes[4]{};
    if (offset > size || size - offset < 4)
        return false;
    for (unsigned i = 0; i < 4; ++i)
        if (!read8(base, size, offset + i, bytes[i]))
            return false;
    result = le32(bytes);
    return true;
}

inline bool probe(const volatile uint8_t* base, uint64_t bar_size, probe_result& result) {
    result = {};
    uint8_t cap_length = 0;
    uint16_t version = 0;
    uint32_t params1 = 0;
    uint32_t params2 = 0;
    uint32_t hccparams = 0;
    uint32_t doorbell = 0;
    uint32_t runtime = 0;
    if (!read8(base, bar_size, 0, cap_length) || !read16(base, bar_size, 2, version) ||
        !read32(base, bar_size, 4, params1) || !read32(base, bar_size, 8, params2) ||
        !read32(base, bar_size, 0x10, hccparams) || !read32(base, bar_size, 0x14, doorbell) ||
        !read32(base, bar_size, 0x18, runtime))
        return false;
    if (cap_length < 0x20 || cap_length >= bar_size || (doorbell & 3u) != 0 ||
        (runtime & 0x1fu) != 0)
        return false;
    const uint8_t max_slots = static_cast<uint8_t>(params1 & 0xffu);
    const uint16_t max_interrupters = static_cast<uint16_t>((params1 >> 8) & 0x7ffu);
    const uint8_t max_ports = static_cast<uint8_t>(params1 >> 24);
    if (max_slots == 0 || max_interrupters == 0 || max_ports == 0)
        return false;
    const uint16_t scratchpads = static_cast<uint16_t>(((params2 >> 21) & 0x1fu) << 5 |
                                                       ((params2 >> 27) & 0x1fu));
    const uint64_t operational = cap_length;
    uint32_t status = 0;
    uint32_t page_sizes = 0;
    uint32_t configure = 0;
    uint32_t port_status = 0;
    if (!read32(base, bar_size, operational + 4, status) ||
        !read32(base, bar_size, operational + 8, page_sizes) ||
        !read32(base, bar_size, operational + 0x38, configure) ||
        !read32(base, bar_size, operational + 0x400, port_status))
        return false;
    result.capabilities.capability_length = cap_length;
    result.capabilities.interface_version = version;
    result.capabilities.max_slots = max_slots;
    result.capabilities.max_interrupters = max_interrupters;
    result.capabilities.max_ports = max_ports;
    result.capabilities.max_scratchpad_buffers = scratchpads;
    result.capabilities.context_size_64 = (hccparams & (1u << 2)) != 0;
    result.capabilities.address_64 = (hccparams & 1u) != 0;
    result.capabilities.doorbell_offset = doorbell & ~3u;
    result.capabilities.runtime_offset = runtime & ~0x1fu;
    result.capabilities.operational_status = status;
    result.capabilities.page_size_mask = page_sizes & 0xffffu;
    result.capabilities.configure = configure;
    result.capabilities.first_port_status = port_status;
    result.capabilities.ready = (status & (1u << 11)) == 0;
    result.capabilities.halted = (status & 1u) != 0;
    result.capabilities.page_size_4k = (page_sizes & 1u) != 0;
    result.valid = result.capabilities.ready && result.capabilities.page_size_4k;
    result.state = enumeration_state::unsupported_no_transfer_engine;
    return result.valid;
}

}
