#pragma once

#include <astia/common.hpp>

namespace astia::plat::aarch64_virt {
    /// QEMU virt guarantees RAM at 0x4000_0000 and places a bare-metal DTB at
    /// the start of RAM.  See docs/aarch64-virt-boot.md for the QEMU contract.
    inline constexpr u64 ram_base        = 0x40000000ULL;
    inline constexpr u64 dtb_address     = ram_base;
    inline constexpr u32 dtb_window_size = 0x00100000U;

    /// PL011 fallback only.  The boot stage prefers the UART described by
    /// /chosen/stdout-path.  QEMU documents that device addresses other than
    /// RAM and flash must be read from the DTB.
    inline constexpr u64 pl011_base = 0x09000000ULL;
    inline constexpr u32 pl011_size = 0x00001000U;
} // namespace astia::plat::aarch64_virt
