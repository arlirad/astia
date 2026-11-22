#pragma once

#include <astia/arch/mmio.hpp>
#include <astia/resource_limits.hpp>

#include "pci_mmio_policy.hpp"

namespace astia::arch::x86_64 {

    inline constexpr u32 pci_mmio_owner_capacity = resource_limits::policy::pci_resources;

    void pci_mmio_owner_configure(u64 alias_floor, pci_mmio_va_limits limits);

    bool                pci_mmio_owner_map(u64 physical, u64 size, bool prefetchable,
                                           u64& virtual_address);
    void                pci_mmio_owner_unmap(u64 virtual_address, u64 size);
    mmio_borrow_status  pci_mmio_owner_borrow_status(u64 base, u64 mapped_bytes);
    mmio_reserve_result pci_mmio_owner_reserve_generic(u64 base, u64 mapped_bytes,
                                                       u64 virtual_address);
    void pci_mmio_owner_release_generic(u64 virtual_address, u64 mapped_bytes);

} // namespace astia::arch::x86_64
