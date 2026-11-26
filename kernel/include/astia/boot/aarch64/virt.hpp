#pragma once

#include <astia/common.hpp>
#include <astia/mm/memory_map.hpp>

namespace astia::boot::aarch64::virt {
    inline constexpr usize console_path_capacity = 256;

    enum class console_source : u8 {
        device_tree,
        platform_default,
    };

    struct console_info {
        u64            base   = 0;
        u32            size   = 0;
        console_source source = console_source::platform_default;
        char           path[console_path_capacity]{};
    };

    struct gic_info {
        u64  distributor_base      = 0;
        u64  distributor_size      = 0;
        u64  redistributor_base    = 0;
        u64  redistributor_size    = 0;
        u32  redistributor_regions = 0;
        bool present               = false;
    };

    /// Typed boundary between the flattened device tree and the AArch64 early
    /// boot stage.  Only fields this stage consumes are represented; later
    /// boot stages grow this type instead of exposing parser internals.
    struct boot_info {
        const u8*      dtb      = nullptr;
        u32            dtb_size = 0;
        mm::memory_map memory; ///< Usable RAM ranges from /memory nodes.
        mm::memory_map
                     reserved; ///< Memory reservation block plus static /reserved-memory.
        console_info console; ///< Boot console selected from /chosen or platform default.
        gic_info     gic;     ///< Validated GICv3 distributor/redistributor windows.
    };
} // namespace astia::boot::aarch64::virt
