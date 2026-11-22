#include "pci_mmio_owner.hpp"

#include <astia/arch/layout.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/core/spinlock.hpp>
#include <astia/log.hpp>
#include <astia/memory.hpp>

namespace astia::arch::x86_64 {

    namespace {

        using logs = astia::log::context<"arch">;

        u64                pci_mmio_alias_floor = pci_identity_window_size;
        pci_mmio_va_limits pci_mmio_limits{};

        enum class mmio_reservation_owner : u8 {
            pci,
            generic,
        };

        struct mmio_reservation {
            u64                    first           = 0;
            u64                    last            = 0;
            u64                    virtual_address = 0;
            u64                    size            = 0;
            mmio_reservation_owner owner           = mmio_reservation_owner::pci;
            bool                   used            = false;
        };

        core::spinlock   mmio_reservation_lock;
        mmio_reservation mmio_reservations[pci_mmio_owner_capacity]{};

        mmio_reservation* find_mmio_reservation(u64 virtual_address, u64 size) {
            for (auto& item : mmio_reservations) {
                if (item.used && item.virtual_address == virtual_address &&
                    item.size == size)
                    return &item;
            }
            return nullptr;
        }

        mmio_reservation* free_mmio_reservation() {
            for (auto& item : mmio_reservations) {
                if (!item.used)
                    return &item;
            }
            return nullptr;
        }

    } // namespace

    void pci_mmio_owner_configure(u64 alias_floor, pci_mmio_va_limits limits) {
        core::spinlock_guard guard(mmio_reservation_lock);
        for (auto& item : mmio_reservations)
            item = {};
        pci_mmio_alias_floor = alias_floor;
        pci_mmio_limits      = limits;
    }

    bool pci_mmio_owner_map(u64 physical, u64 size, bool prefetchable,
                            u64& virtual_address) {
        core::spinlock_guard guard(mmio_reservation_lock);

        const pci_mmio_range range =
            pci_mmio_range_check(physical, size, pci_mmio_alias_floor);
        if (range == pci_mmio_range::invalid) {
            logs::error("PCI BAR refused: invalid range phys=%llx size=%llx", physical,
                        size);
            return false;
        }
        if (range == pci_mmio_range::write_back_alias) {
            logs::error("PCI BAR refused: phys=%llx size=%llx page=%llx "
                        "overlaps write-back alias below %llx",
                        physical, size, physical & ~static_cast<u64>(mm::page_mask),
                        pci_mmio_alias_floor);
            return false;
        }

        const pci_mmio_page_span span = pci_mmio_page_span_of(physical, size);
        if (!pci_mmio_range_fits_direct_map(span.first, span.last, pci_mmio_limits)) {
            logs::error("PCI BAR refused: phys=%llx size=%llx "
                        "page=%llx-%llx outside direct-map segment",
                        physical, size, span.first, span.last);
            return false;
        }

        auto* map = kernel_page_map();
        if (map == nullptr)
            return false;

        for (const auto& item : mmio_reservations) {
            if (!item.used)
                continue;
            if (pci_mmio_spans_overlap(span, {item.first, item.last})) {
                logs::error("PCI BAR refused: phys=%llx size=%llx "
                            "page=%llx-%llx overlaps active MMIO reservation "
                            "va=%llx size=%llx",
                            physical, size, span.first, span.last, item.virtual_address,
                            item.size);
                return false;
            }
        }

        mmio_reservation* slot = free_mmio_reservation();
        if (slot == nullptr) {
            logs::error("PCI BAR refused: mapping table full phys=%llx size=%llx",
                        physical, size);
            return false;
        }

        for (u64 page = span.first;; page += mm::page_size) {
            if (map->translate(kernel_base + page) != page_map::invalid) {
                logs::error("PCI BAR refused: phys=%llx size=%llx "
                            "target=%llx already mapped",
                            physical, size, kernel_base + page);
                return false;
            }
            if (page == span.last)
                break;
        }

        const page_flags flags = pci_bar_page_flags(prefetchable);

        for (u64 page = span.first;; page += mm::page_size) {
            if (!map->map_4k(kernel_base + page, page, flags)) {
                for (u64 rollback = span.first; rollback < page;
                     rollback += mm::page_size)
                    map->unmap(kernel_base + rollback);
                return false;
            }
            if (page == span.last)
                break;
        }

        const u64 mapped = kernel_base + physical;
        *slot = {span.first, span.last, mapped, size, mmio_reservation_owner::pci, true};
        virtual_address = mapped;
        return true;
    }

    void pci_mmio_owner_unmap(u64 virtual_address, u64 size) {
        core::spinlock_guard guard(mmio_reservation_lock);

        mmio_reservation* mapping = find_mmio_reservation(virtual_address, size);
        if (mapping == nullptr || mapping->owner != mmio_reservation_owner::pci) {
            logs::error("PCI unmap refused: no mapping va=%llx size=%llx",
                        virtual_address, size);
            return;
        }

        auto* map = kernel_page_map();
        if (map == nullptr) {
            logs::error("PCI unmap refused: no page map va=%llx size=%llx",
                        virtual_address, size);
            return;
        }

        for (u64 page = mapping->first;; page += mm::page_size) {
            map->unmap(kernel_base + page);
            if (page == mapping->last)
                break;
        }
        *mapping = {};
    }

    mmio_borrow_status pci_mmio_owner_borrow_status(u64 base, u64 mapped_bytes) {
        core::spinlock_guard guard(mmio_reservation_lock);

        if (mapped_bytes == 0 || (base & mm::page_mask) != 0 ||
            (mapped_bytes & mm::page_mask) != 0 || base > ~u64{0} - (mapped_bytes - 1))
            return mmio_borrow_status::out_of_range;

        const pci_mmio_page_span span = pci_mmio_page_span_of(base, mapped_bytes);
        if (!pci_mmio_range_fits_direct_map(span.first, span.last, pci_mmio_limits))
            return mmio_borrow_status::out_of_range;

        for (const auto& item : mmio_reservations) {
            if (!item.used || !pci_mmio_spans_overlap(span, {item.first, item.last}))
                continue;
            return item.owner == mmio_reservation_owner::pci
                       ? mmio_borrow_status::pci_owned
                       : mmio_borrow_status::generic_owned;
        }

        auto* map = kernel_page_map();
        if (map == nullptr)
            return mmio_borrow_status::absent;

        bool all_direct = true;
        bool aliased    = false;
        for (u64 page = span.first;; page += mm::page_size) {
            const bool identity = map->translate(page) == page;
            if (identity)
                aliased = true;

            if (map->translate(kernel_base + page) == page) {
                aliased = true;
            }
            else {
                all_direct = false;
            }

            if (page == span.last)
                break;
        }

        if (all_direct)
            return mmio_borrow_status::borrowable;
        return mmio_borrow_status_for(false, false, false, aliased);
    }

    mmio_reserve_result pci_mmio_owner_reserve_generic(u64 base, u64 mapped_bytes,
                                                       u64 virtual_address) {
        core::spinlock_guard guard(mmio_reservation_lock);

        if (mapped_bytes == 0 || (base & mm::page_mask) != 0 ||
            (mapped_bytes & mm::page_mask) != 0 || base > ~u64{0} - (mapped_bytes - 1))
            return mmio_reserve_result::invalid;

        const pci_mmio_page_span span = pci_mmio_page_span_of(base, mapped_bytes);
        for (const auto& item : mmio_reservations) {
            if (item.used && pci_mmio_spans_overlap(span, {item.first, item.last}))
                return mmio_reserve_result::busy;
        }

        mmio_reservation* slot = free_mmio_reservation();
        if (slot == nullptr)
            return mmio_reserve_result::full;

        *slot = {span.first,
                 span.last,
                 virtual_address,
                 mapped_bytes,
                 mmio_reservation_owner::generic,
                 true};
        return mmio_reserve_result::ok;
    }

    void pci_mmio_owner_release_generic(u64 virtual_address, u64 mapped_bytes) {
        core::spinlock_guard guard(mmio_reservation_lock);

        mmio_reservation* mapping = find_mmio_reservation(virtual_address, mapped_bytes);
        if (mapping == nullptr || mapping->owner != mmio_reservation_owner::generic) {
            logs::error("generic MMIO release refused: no reservation va=%llx size=%llx",
                        virtual_address, mapped_bytes);
            return;
        }
        *mapping = {};
    }

} // namespace astia::arch::x86_64
