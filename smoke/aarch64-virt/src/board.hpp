#pragma once

#include "types.hpp"

namespace astia::smoke {
    // QEMU virt guarantees RAM at 0x4000_0000 and passes the bare-metal DTB
    // there.  PL011 is fixed for this first milestone; see README.md.
    inline constexpr u64 qemu_virt_ram_base        = 0x40000000ULL;
    inline constexpr u64 qemu_virt_dtb_address     = qemu_virt_ram_base;
    inline constexpr u32 qemu_virt_dtb_window_size = 0x00100000U;
    inline constexpr u64 qemu_virt_pl011_base      = 0x09000000ULL;
} // namespace astia::smoke
