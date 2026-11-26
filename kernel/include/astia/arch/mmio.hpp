#pragma once

#include <astia/arch/page_map.hpp>
#include <astia/common.hpp>

namespace astia::arch {
    struct mmio_window_bounds {
        vaddr_t base;
        vaddr_t limit;
    };

    mmio_window_bounds mmio_window();

    /// Strict uncached device MMIO flags. PCD+PWT with the PAT bit clear
    /// selects PAT entry 3, which is UC under the reset-compatible IA32_PAT.
    constexpr page_flags mmio_device_page_flags() {
        return page_flags::present | page_flags::writable | page_flags::cache_disable |
               page_flags::write_through;
    }

    enum class mmio_borrow_status {
        absent,
        borrowable,
        pci_owned,
        generic_owned,
        aliased,
        out_of_range,
    };

    constexpr mmio_borrow_status mmio_borrow_status_for(bool pci_owned,
                                                        bool generic_owned,
                                                        bool direct_mapped,
                                                        bool aliased) {
        if (pci_owned)
            return mmio_borrow_status::pci_owned;

        if (generic_owned)
            return mmio_borrow_status::generic_owned;

        if (direct_mapped)
            return mmio_borrow_status::borrowable;

        if (aliased)
            return mmio_borrow_status::aliased;

        return mmio_borrow_status::absent;
    }

    enum class mmio_reserve_result {
        ok,
        busy,
        full,
        invalid,
    };

    /// Classify a page-aligned physical extent for direct-map borrowing.
    /// Atomic with PCI BAR map/unmap: a reserved extent must not be borrowed
    /// without ownership.
    mmio_borrow_status mmio_borrow_status_of(u64 base, u64 mapped_bytes);

    /// Reserve a page-aligned physical extent for an MMIO-window mapping.
    /// Atomic with PCI BAR map/unmap and other generic reservations.
    mmio_reserve_result mmio_reserve_generic(u64 base, u64 mapped_bytes,
                                             u64 virtual_address);

    /// Release a generic reservation by its exact virtual address and size.
    void mmio_release_generic(u64 virtual_address, u64 mapped_bytes);

    /// Map an owning PCI BAR through the fail-closed arch PCI mapper.
    bool pci_mmio_map(u64 physical, u64 size, bool prefetchable, u64& virtual_address);

    /// Release an owning PCI BAR mapping returned by `pci_mmio_map`.
    void pci_mmio_unmap(u64 virtual_address, u64 size);
} // namespace astia::arch
