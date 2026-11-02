#pragma once

#include <astia/boot.hpp>
#include <astia/span.hpp>

namespace astia::boot::virt {

    /// Parse the physical Multiboot2 information structure passed in EBX.
    /// All tag and variable-length fields are checked before they are read.
    bool parse_multiboot2_info(span<const u8> mbi, boot_info& out);
    inline bool parse_multiboot2_info(const void* mbi, boot_info& out) {
        if (mbi == nullptr)
            return false;
        const auto* bytes = static_cast<const u8*>(mbi);
        return parse_multiboot2_info(
            span<const u8>(bytes, *reinterpret_cast<const u32*>(bytes)), out);
    }

} // namespace astia::boot::virt
