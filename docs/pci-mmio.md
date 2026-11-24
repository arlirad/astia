# PCI BAR MMIO mapping

The x86_64 kernel PCI mapper (`kernel/src/arch/x86_64/init.cpp`) only
maps BARs whose page-rounded physical start is at or above the write-back
alias floor:

```
floor = max(1 GiB identity window, page-aligned top of usable RAM)
```

Everything below that floor already has a write-back alias through the boot
identity window or the `kernel_base` direct map. The mapper rejects such BARs
instead of changing the memory type of the existing alias, because the kernel
has no SMP TLB/cache shootdown mechanism and remapping an alias in place would
leave other CPUs with stale translations.

Fresh mappings above the floor are strict uncached: `PCD+PWT` with the PAT bit
clear selects PAT index 3, which is UC under the reset-compatible IA32_PAT
(and `PCD=1` is UC when PAT is disabled). The kernel does not initialize or
verify IA32_PAT, so this relies on the reset/firmware value of PAT entry 3;
that residual assumption is not checked at runtime. PCI prefetchable is not
permission for generic write-back caching; a future write-combining policy
for prefetchable BARs would need a separate explicit mapping API.

The page-rounded BAR extent must also fit the direct-map segment before any
PTE is installed. The page-rounded span is validated in O(1) with endpoint
and boundary arithmetic: every page covered by the contiguous bounds must be
canonical, below `direct_map_limit` (one PML4 slot on x86_64), and below the
lowest reserved higher-half window (`mmio_window`, `recursive_base`, and
`image_base`). This prevents arithmetic wraparound or collisions with the
user half, recursive window, image region, or generic MMIO window, including
multi-page spans that cross a boundary; refusal uses a distinct `arch` error
log line.

A BAR is refused, with an `arch` error log line, when its range is invalid,
overflows, or its page-rounded start overlaps the alias window. Before any
mapping is installed, the mapper also walks the page-rounded target virtual
range and refuses it if `translate(kernel_base + page)` already resolves.
This covers pre-existing `kernel_base + physical` mappings such as the
framebuffer, not just the floor. In every refusal path no page-table entry is
changed and no virtual address is returned.

Mapping uses two locks in one strict order: `mmio_lock` protects the generic
MMIO-window allocator, and `mmio_reservation_lock` protects the shared
PCI/generic ownership table. Generic map and unmap take
`mmio_lock` → `mmio_reservation_lock`; PCI-only map/unmap/query paths take
only `mmio_reservation_lock`. A fixed table of
`resource_limits::policy::pci_resources` entries records each active
reservation's owner (PCI or generic), page-rounded first/last physical page,
returned virtual address, and size. A new BAR is refused if any page in
its rounded span overlaps an active entry, even when the two BARs' byte ranges
are disjoint within one 4 KiB page. A full table is refused with an explicit
error. PCI unmap looks up the exact virtual address and size for a PCI-owned
entry; generic release does the same for a generic entry. Each removes only
that reservation's pages and then clears its record, so it cannot remove a
page belonging to another mapping.

BAR and generic MMIO-window mappings are therefore page-exclusive for their
entire lifetime. There is no SMP TLB/cache shootdown, so pages are never
shared, retyped, or remapped in place.

`mmio_mapping::map` may borrow `kernel_base + physical` only when the arch
query reports the page-rounded extent as borrowable: every page resolves
through the direct map and no active PCI or generic reservation owns it.
PCI-owned, generic-owned, `aliased`, or `out_of_range` extents return
`error_t::ebusy` instead of producing an alias without ownership. `aliased`
means some page already has an identity or direct mapping but the extent is
not wholly borrowable; generic MMIO must not install a second cache type over
that range. `out_of_range` is rejected by the O(1) span check before any
per-page translate scan. Only a truly absent extent falls through to the
MMIO-window path,
which first reserves the physical span in the shared table under the same
lock before any `map_4k`; a concurrent PCI map therefore cannot claim the
same pages. Fresh generic MMIO-window mappings use
`arch::mmio_device_page_flags()` (strict PCD+PWT), the same device policy as
fresh PCI mappings. The MMIO VA cursor advances only after every page maps
successfully; a partial failure unmaps this call's pages, releases its
reservation, and leaves the cursor at the failed VA for safe reuse.

Generic allocation queries ownership and reserves under `mmio_lock` and then
the shared reservation lock, so the ownership decision and transition are
atomic with generic state. PCI map/unmap and standalone borrow queries take
only the reservation lock. No path takes the reservation lock and then
`mmio_lock`, so the order is always `mmio_lock` → reservation lock.

The public `pci_bar::map()` helper validates and page-normalizes the request,
then routes it through `arch::pci_mmio_map`: the same fail-closed PCI mapper
used by `hw::pci_map_bar()`. It therefore enforces the direct-map floor,
canonical/reserved VA bounds, page-rounded ownership, and strict PCD+PWT
policy, and returns `error_t::ebusy` on refusal. The returned
`mmio_mapping` is PCI-owned and releases through `arch::pci_mmio_unmap`; it
never uses the generic window path, never hands off or retypes an existing
PTE, and never releases a generic reservation. A borrow-only generic
`mmio_mapping` has no `mapped_bytes_` and releases without unmapping a page
or touching a reservation.

## Verification scope

`kernel/tests/pci_test.cpp` compiles `dev/mmio.cpp` in host mode and replaces
the real x86 ownership query, arch PCI map/unmap, page map, and config
backend with deterministic fakes. It exercises the live `hw::pci_bar::map()`
and `mmio_mapping` control flow, including EBUSY, rollback, release ordering,
and page-sharing conflicts, but it does not prove the production
`pci_mmio_borrow_status` implementation, real PTE ordering, or arch lock
behaviour.

`kernel/tests/pci_mmio_policy_test.cpp` exercises the pure range/cache policy
helpers. `kernel/tests/pci_mmio_owner_test.cpp` compiles the real extracted
ownership table and PCI mapper with deterministic fake page-map/log seams; it
covers capacity-full, exact release, partial rollback, PCI/generic conflicts,
both release orders, borrow status, and high-span rejection before translate.
The experimental AArch64 target is compile-only and does not provide AArch64
runtime behaviour. Target configuration and architecture-boundary checks are
separate from these functional PCI/MMIO tests.
