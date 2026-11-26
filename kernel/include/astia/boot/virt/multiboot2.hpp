#pragma once

#include <astia/boot.hpp>
#include <astia/span.hpp>

namespace astia::boot::virt {
    /// Parse the physical Multiboot2 information structure passed in EBX.
    /// All tag and variable-length fields are checked before they are read.
    bool parse_multiboot2_info(span<const u8> mbi, boot_info& out);
} // namespace astia::boot::virt
