#pragma once

#include <astia/boot.hpp>
#include <astia/span.hpp>

namespace astia::boot::virt {

    /// Interpret the x86/HVM direct boot ABI ("PVH") start_info at
    /// `start_info` and fill `out` with the bootloader-agnostic handoff it
    /// carries: memory map, command line, RSDP and modules.
    ///
    /// `start_info` is the physical address QEMU's PVH option ROM leaves in
    /// %ebx (see pc-bios/optionrom/pvh_main.c); our boot trampoline forwards
    /// it to kmain and it is reachable through the identity map.
    ///
    /// Returns false if the pointer is null, the magic is wrong, or the
    /// structure carries no memory map. `out` is left untouched on failure.
    bool parse_start_info(span<const u8> start_info, boot_info& out);
    inline bool parse_start_info(const void* start_info, boot_info& out) {
        return parse_start_info(
            span<const u8>(static_cast<const u8*>(start_info), 56), out);
    }

} // namespace astia::boot::virt
