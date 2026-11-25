#include <astia/arch/layout.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/log.hpp>

#include "../src/arch/x86_64/pci_mmio_owner.hpp"
#include "test.hpp"

#include <array>

namespace {

    using astia::paddr_t;
    using astia::vaddr_t;
    using astia::arch::mmio_borrow_status;
    using astia::arch::mmio_reserve_result;
    using astia::arch::page_flags;
    using astia::arch::page_map;
    using astia::arch::x86_64::pci_mmio_owner_borrow_status;
    using astia::arch::x86_64::pci_mmio_owner_capacity;
    using astia::arch::x86_64::pci_mmio_owner_configure;
    using astia::arch::x86_64::pci_mmio_owner_map;
    using astia::arch::x86_64::pci_mmio_owner_release_generic;
    using astia::arch::x86_64::pci_mmio_owner_reserve_generic;
    using astia::arch::x86_64::pci_mmio_owner_unmap;

    constexpr astia::u64 kernel_base      = 0xFFFF800000000000ull;
    constexpr astia::u64 alias_floor      = 0x40000000ull;
    constexpr astia::u64 direct_map_limit = 1ull << 39;

    struct fake_mapping {
        vaddr_t virt = 0;
        paddr_t phys = 0;
        bool    used = false;
    };

    std::array<fake_mapping, 512> fake_mappings{};
    astia::u32                    fake_map_calls        = 0;
    astia::u32                    fake_unmap_calls      = 0;
    astia::u32                    fake_translate_calls  = 0;
    astia::u32                    fake_fail_on_map_call = ~astia::u32{0};

    astia::arch::page_map fake_page_map;

    void reset_fake_page_map() {
        for (auto& entry : fake_mappings)
            entry = {};
        fake_map_calls        = 0;
        fake_unmap_calls      = 0;
        fake_translate_calls  = 0;
        fake_fail_on_map_call = ~astia::u32{0};
    }

    void add_fake_mapping(vaddr_t virt, paddr_t phys) {
        for (auto& entry : fake_mappings) {
            if (!entry.used) {
                entry = {virt, phys, true};
                return;
            }
        }
    }

    bool configured() {
        pci_mmio_owner_configure(alias_floor,
                                 {kernel_base, direct_map_limit, direct_map_limit});
        return true;
    }

} // namespace

namespace astia::arch {

    page_map* kernel_page_map() {
        return &fake_page_map;
    }

    page_map::~page_map() {}

    bool page_map::map_4k(vaddr_t virt, paddr_t phys, page_flags) {
        ++fake_map_calls;
        if (fake_map_calls == fake_fail_on_map_call)
            return false;
        add_fake_mapping(virt, phys);
        return true;
    }

    bool page_map::unmap(vaddr_t virt) {
        ++fake_unmap_calls;
        for (auto& entry : fake_mappings) {
            if (entry.used && entry.virt == virt) {
                entry = {};
                return true;
            }
        }
        return false;
    }

    paddr_t page_map::translate(vaddr_t virt) const {
        ++fake_translate_calls;
        for (const auto& entry : fake_mappings) {
            if (entry.used && entry.virt == virt)
                return entry.phys;
        }
        return page_map::invalid;
    }

} // namespace astia::arch

namespace astia::log::detail {

    void emit(level, const char*, astia::u32) {}
    void prefix(record_builder&, level, const char*, astia::u32) {}

} // namespace astia::log::detail

ASTIA_TEST(pci_mmio_owner_map_and_exact_release) {
    reset_fake_page_map();
    configured();

    constexpr astia::u64 physical = 0x1'0000'0000ull;
    astia::u64           va       = 0;
    ASTIA_CHECK(pci_mmio_owner_map(physical, 0x1000, false, va));
    ASTIA_CHECK(va == kernel_base + physical);
    ASTIA_CHECK(!pci_mmio_owner_map(physical, 0x1000, false, va));

    pci_mmio_owner_unmap(va, 0x1000);
    ASTIA_CHECK(pci_mmio_owner_map(physical, 0x1000, false, va));
    pci_mmio_owner_unmap(va, 0x1000);
}

ASTIA_TEST(pci_mmio_owner_capacity_is_enforced) {
    reset_fake_page_map();
    configured();

    astia::u64 va = 0;
    for (astia::u32 index = 0; index < pci_mmio_owner_capacity; ++index) {
        ASTIA_CHECK(
            pci_mmio_owner_map(0x1'0000'0000ull + index * 0x2000, 0x1000, false, va));
    }
    ASTIA_CHECK(!pci_mmio_owner_map(0x1'0000'0000ull + pci_mmio_owner_capacity * 0x2000,
                                    0x1000, false, va));
}

ASTIA_TEST(pci_mmio_owner_partial_map_rolls_back) {
    reset_fake_page_map();
    configured();
    fake_fail_on_map_call = 2;

    astia::u64 va = 0;
    ASTIA_CHECK(!pci_mmio_owner_map(0x1'0000'0000ull, 0x2000, false, va));
    ASTIA_CHECK(fake_unmap_calls == 1);

    fake_fail_on_map_call = ~astia::u32{0};
    ASTIA_CHECK(pci_mmio_owner_map(0x1'0000'0000ull, 0x2000, false, va));
    pci_mmio_owner_unmap(va, 0x2000);
}

ASTIA_TEST(pci_mmio_owner_borrow_and_high_rejection) {
    reset_fake_page_map();
    configured();

    constexpr astia::u64 physical = 0x1'0000'0000ull;
    add_fake_mapping(kernel_base + physical, physical);
    ASTIA_CHECK(pci_mmio_owner_borrow_status(physical, 0x1000) ==
                mmio_borrow_status::borrowable);

    reset_fake_page_map();
    ASTIA_CHECK(pci_mmio_owner_borrow_status(0x7ffffffff000ull, 0x2000) ==
                mmio_borrow_status::out_of_range);
    ASTIA_CHECK(fake_translate_calls == 0);
}

ASTIA_TEST(pci_mmio_owner_huge_span_rejected_before_translate) {
    reset_fake_page_map();
    configured();

    ASTIA_CHECK(pci_mmio_owner_borrow_status(0x1'0000'0000ull, 0x4000'0000'0000ull) ==
                mmio_borrow_status::out_of_range);
    ASTIA_CHECK(fake_translate_calls == 0);
}

ASTIA_TEST(pci_mmio_owner_aliased_identity_is_rejected_for_generic) {
    reset_fake_page_map();
    configured();

    constexpr astia::u64 physical = 0x1'0000'0000ull;
    add_fake_mapping(physical, physical);
    ASTIA_CHECK(pci_mmio_owner_borrow_status(physical, 0x1000) ==
                mmio_borrow_status::aliased);
}

ASTIA_TEST(pci_mmio_owner_pci_generic_conflict_and_release_orders) {
    reset_fake_page_map();
    configured();

    constexpr astia::u64 physical = 0x1'0000'0000ull;
    ASTIA_CHECK(pci_mmio_owner_reserve_generic(physical, 0x1000, 0xffffc00000001000ull) ==
                mmio_reserve_result::ok);
    ASTIA_CHECK(pci_mmio_owner_borrow_status(physical, 0x1000) ==
                mmio_borrow_status::generic_owned);
    astia::u64 va = 0;
    ASTIA_CHECK(!pci_mmio_owner_map(physical, 0x1000, false, va));
    pci_mmio_owner_release_generic(0xffffc00000001000ull, 0x1000);

    ASTIA_CHECK(pci_mmio_owner_map(physical, 0x1000, false, va));
    ASTIA_CHECK(pci_mmio_owner_borrow_status(physical, 0x1000) ==
                mmio_borrow_status::pci_owned);
    ASTIA_CHECK(pci_mmio_owner_reserve_generic(physical, 0x1000, 0xffffc00000002000ull) ==
                mmio_reserve_result::busy);
    pci_mmio_owner_unmap(va, 0x1000);
    ASTIA_CHECK(pci_mmio_owner_reserve_generic(physical, 0x1000, 0xffffc00000003000ull) ==
                mmio_reserve_result::ok);
    pci_mmio_owner_release_generic(0xffffc00000003000ull, 0x1000);
}
