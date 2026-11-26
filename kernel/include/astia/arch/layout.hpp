#pragma once

#include <astia/common.hpp>

namespace astia::arch {
    /// Lowest virtual address of the kernel's address space, provided by the
    /// selected arch (src/arch/<arch>/layout.cpp): the start of the canonical
    /// higher half on x86_64 (0xFFFF800000000000). Also the base of the
    /// direct physical map — physical address `p` is reachable at
    /// `kernel_base + p`.
    extern const u64 kernel_base;

    /// Where the kernel image itself is linked, provided by the selected
    /// arch. Separate from kernel_base because static code/data must sit in
    /// the top 2 GiB for -mcmodel=kernel sign-extended addressing (x86_64:
    /// 0xFFFFFFFF80000000); the image lives at `image_base + physical
    /// address`. Must match the linker script
    /// (cmake/linker/virt-x86_64.ld) and the boot page tables (boot.S).
    extern const u64 image_base;

    /// Window opened by the arch's recursive-mapping slot, provided by the
    /// selected arch: every page map reserves that one top-level slot
    /// pointing back at its *own* root, so the tables of the active address
    /// space become reachable by arithmetic on virtual addresses alone —
    /// table access needs no 1:1 (identity) mapping. 0 when the arch has
    /// no such window (x86_64: PML4 slot 510 << 39 = 0xFFFFFF0000000000;
    /// see src/arch/x86_64/page_map.cpp).
    extern const u64 recursive_base;

    /// Exclusive physical-offset limit of the direct-map segment starting at
    /// `kernel_base`: `kernel_base + physical` stays inside that segment only
    /// while `physical < direct_map_limit`. x86_64: one PML4 slot (512 GiB);
    /// 0 when the arch has no direct map.
    extern const u64 direct_map_limit;
} // namespace astia::arch
