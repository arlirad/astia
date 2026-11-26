#pragma once

#include <astia/boot/aarch64/virt.hpp>

namespace astia::boot::aarch64::fdt {
    enum class error : u8 {
        none,
        bad_magic,
        too_small,
        bad_version,
        bad_bounds,
        bad_alignment,
        bad_structure,
        bad_token,
        depth_overflow,
        bad_string,
        bad_name,
        bad_cells,
        too_many_regions,
        no_memory,
        bad_gic,
    };

    const char* describe(error value);

    /// Trusted platform console rule.  The parser only accepts a
    /// `/chosen/stdout-path` PL011 candidate when its `reg` matches this exact
    /// MMIO base and covers at least this many bytes.  The caller supplies the
    /// values from its platform layer; the DTB never chooses where the stage
    /// writes.
    struct console_policy {
        u64 base         = 0;
        u32 minimum_size = 0;
    };

    /// Parses an untrusted flattened device tree.  `window_size` bounds every
    /// read; the blob's own totalsize must fit inside it.  On success `info`
    /// carries only supported fields; on failure its contents are unspecified.
    error parse(const u8* blob, u32 window_size, console_policy console,
                virt::boot_info& info);
} // namespace astia::boot::aarch64::fdt
