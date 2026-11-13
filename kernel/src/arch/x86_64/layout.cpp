#include <astia/arch/layout.hpp>
#include <astia/arch/mmio.hpp>

namespace astia::arch {

    // x86_64 layout (4-level paging):
    //   kernel_base    = start of the canonical higher half (PML4 slot 256,
    //                    PDPT index 0); also the direct physical map
    //                    (virt = base + phys).
    //   image_base     = top 2 GiB, where the kernel image links (PML4 slot
    //                    511, PDPT index 510), so static code/data stay
    //                    reachable through sign-extended addressing
    //                    (-mcmodel=kernel).
    //   recursive_base = window of PML4 slot 510, which every page map
    //                    reserves to point back at its own root (recursive
    //                    mapping: table access without 1:1 windows).
    // Must agree with boot.S (boot page tables) and
    // cmake/linker/virt-x86_64.ld.
    const u64 kernel_base    = 0xFFFF800000000000ull;
    const u64 image_base     = 0xFFFFFFFF80000000ull;
    const u64 recursive_base = 0xFFFFFF0000000000ull;

    mmio_window_bounds mmio_window() {
        return {0xFFFFC00000000000ull, 0xFFFFF00000000000ull};
    }

} // namespace astia::arch
