#include <astia/api/errno.h>
#include <astia/arch/cpu.hpp>
#include <astia/arch/layout.hpp>
#include <astia/arch/mmio.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/arch/pci_config.hpp>
#include <astia/dev/driver.hpp>
#include <astia/dev/mmio.hpp>
#include <astia/hw/pci.hpp>

#include "task_host.hpp"
#include "test.hpp"

#include <cstdio>

namespace {
    bool                            fake_page_map_ready   = false;
    astia::u32                      fake_unmap_count      = 0;
    astia::u32                      fake_map_calls        = 0;
    astia::u32                      fake_reserve_calls    = 0;
    astia::u32                      fake_release_calls    = 0;
    astia::u32                      fake_invalid_release  = 0;
    astia::u32                      fake_map_fail_on_call = ~astia::u32{0};
    astia::u32                      fake_pci_map_calls    = 0;
    astia::u32                      fake_pci_unmap_calls  = 0;
    bool                            fake_pci_map_result   = true;
    bool                            fake_pci_malformed_va = false;
    astia::u64                      fake_pci_unmap_va     = 0;
    astia::u64                      fake_last_map_va      = 0;
    astia::arch::mmio_borrow_status fake_borrow_status =
        astia::arch::mmio_borrow_status::absent;
    astia::arch::mmio_reserve_result fake_reserve_result =
        astia::arch::mmio_reserve_result::ok;
    astia::arch::page_map fake_page_map;

    struct fake_owner {
        astia::u64 first = 0;
        astia::u64 last  = 0;
        astia::u64 va    = 0;
        astia::u64 size  = 0;
        bool       pci   = false;
        bool       used  = false;
    };

    constexpr astia::usize fake_owner_limit = 8;
    fake_owner             fake_owners[fake_owner_limit]{};

    void reset_fake_owners() {
        for (auto& owner : fake_owners) {
            owner = {};
        }

        fake_page_map_ready   = false;
        fake_unmap_count      = 0;
        fake_map_calls        = 0;
        fake_reserve_calls    = 0;
        fake_release_calls    = 0;
        fake_invalid_release  = 0;
        fake_map_fail_on_call = ~astia::u32{0};
        fake_borrow_status    = astia::arch::mmio_borrow_status::absent;
        fake_reserve_result   = astia::arch::mmio_reserve_result::ok;
        fake_pci_map_calls    = 0;
        fake_pci_unmap_calls  = 0;
        fake_pci_map_result   = true;
        fake_pci_malformed_va = false;
        fake_pci_unmap_va     = 0;
        fake_last_map_va      = 0;
    }

    bool fake_overlap(astia::u64 first, astia::u64 last, astia::u64 other_first,
                      astia::u64 other_last) {
        return first <= other_last && other_first <= last;
    }

    struct fake_pci {
        astia::u16 command    = 0;
        bool       probe_low  = false;
        bool       probe_high = false;

        bool selected(astia::arch::pci_bdf address) const {
            return address.bus == 0 && address.device == 1 && address.function == 0;
        }

        astia::u32 bar(astia::u8 offset) const {
            switch (offset) {
            case 0x10:
                return probe_low ? 0xfffff000u : 0x80000000u;
            case 0x14:
                return probe_low ? 0xffffffe1u : 0x0000c001u;
            case 0x18:
                return probe_low ? 0xfffff004u : 0x00000004u;
            case 0x1c:
                return probe_high ? 0xffffffffu : 0x00000001u;
            default:
                return 0;
            }
        }

        astia::u32 read(astia::arch::pci_bdf address, astia::u8 offset) const {
            if (!selected(address))
                return ~astia::u32{0};

            switch (offset) {
            case 0x00:
                return 0x56781234u;
            case 0x04:
                return command;
            case 0x08:
                return 0x01060201u;
            case 0x10:
            case 0x14:
            case 0x18:
            case 0x1c:
                return bar(offset);
            default:
                return 0;
            }
        }

        void write(astia::arch::pci_bdf address, astia::u8 offset, astia::u32 value) {
            if (!selected(address))
                return;

            if (offset == 0x10 || offset == 0x14 || offset == 0x18)
                probe_low = value == ~astia::u32{0};

            if (offset == 0x1c)
                probe_high = value == ~astia::u32{0};

            if (offset == 0x04)
                command = static_cast<astia::u16>(value);
        }
    };

    fake_pci state;
    unsigned mmio_maps   = 0;
    unsigned mmio_unmaps = 0;

    class capture_driver final : public astia::hw::driver {
        public:
        const char* name() const override {
            return "capture";
        }

        bool match(const astia::hw::device_info&) const override {
            return true;
        }

        astia::core::task<astia::i64>
        probe(const astia::hw::device_info& value) override {
            const auto* pci = astia::hw::pci_device_info::get(value);
            if (pci != nullptr)
                captured = pci->device();
            co_return pci != nullptr ? 0 : -ENODEV;
        }

        astia::core::task<astia::i64> start(const astia::hw::device_info&) override {
            co_return 0;
        }

        astia::core::task<void> stop(const astia::hw::device_info&) override {
            co_return astia::error_t::enone;
        }

        astia::core::task<void> remove(const astia::hw::device_info&) override {
            co_return astia::error_t::enone;
        }

        astia::hw::pci_device captured{};
    };

    capture_driver capture;

    bool map_mmio(astia::u64, astia::u64, bool, astia::u64& virtual_address) {
        ++mmio_maps;

        virtual_address = 0xffffc00000001000ull;

        return true;
    }

    void unmap_mmio(astia::u64, astia::u64) {
        ++mmio_unmaps;
    }
} // namespace

namespace astia::arch {
    const u64 kernel_base = 0xFFFF800000000000ull;

    mmio_window_bounds mmio_window() {
        return {0xFFFFC00000000000ull, 0xFFFFF00000000000ull};
    }

    u32 pci_config_read32(pci_bdf address, u16 offset) {
        if (offset > 0xfcu || (offset & 3u) != 0)
            return ~u32{0};

        return state.read(address, static_cast<u8>(offset));
    }

    void pci_config_write32(pci_bdf address, u16 offset, u32 value) {
        if (offset > 0xfcu || (offset & 3u) != 0)
            return;

        state.write(address, static_cast<u8>(offset), value);
    }

    mmio_borrow_status mmio_borrow_status_of(u64 base, u64 mapped_bytes) {
        if (fake_borrow_status != mmio_borrow_status::absent)
            return fake_borrow_status;

        if (mapped_bytes == 0)
            return mmio_borrow_status::absent;

        const u64 first = base & ~static_cast<u64>(mm::page_mask);
        const u64 last  = (base + mapped_bytes - 1) & ~static_cast<u64>(mm::page_mask);

        for (const auto& owner : fake_owners) {
            if (owner.used && fake_overlap(first, last, owner.first, owner.last))
                return owner.pci ? mmio_borrow_status::pci_owned
                                 : mmio_borrow_status::generic_owned;
        }

        return mmio_borrow_status::absent;
    }

    mmio_reserve_result mmio_reserve_generic(u64 base, u64 mapped_bytes,
                                             u64 virtual_address) {
        ++fake_reserve_calls;

        if (fake_reserve_result != mmio_reserve_result::ok)
            return fake_reserve_result;

        const u64 first = base & ~static_cast<u64>(mm::page_mask);
        const u64 last  = (base + mapped_bytes - 1) & ~static_cast<u64>(mm::page_mask);

        for (const auto& owner : fake_owners) {
            if (owner.used && fake_overlap(first, last, owner.first, owner.last))
                return mmio_reserve_result::busy;
        }

        for (auto& owner : fake_owners) {
            if (owner.used)
                continue;

            owner = {first, last, virtual_address, mapped_bytes, false, true};

            return mmio_reserve_result::ok;
        }

        return mmio_reserve_result::full;
    }

    void mmio_release_generic(u64 virtual_address, u64 mapped_bytes) {
        ++fake_release_calls;

        if (mapped_bytes == 0)
            ++fake_invalid_release;

        for (auto& owner : fake_owners) {
            if (owner.used && !owner.pci && owner.va == virtual_address &&
                owner.size == mapped_bytes) {
                owner = {};

                return;
            }
        }
    }

    bool pci_mmio_map(u64 physical, u64, bool, u64& virtual_address) {
        ++fake_pci_map_calls;

        if (!fake_pci_map_result)
            return false;

        virtual_address = fake_pci_malformed_va ? 0 : kernel_base + physical;

        return true;
    }

    void pci_mmio_unmap(u64 virtual_address, u64) {
        ++fake_pci_unmap_calls;

        fake_pci_unmap_va = virtual_address;
    }

    page_map* kernel_page_map() {
        return fake_page_map_ready ? &fake_page_map : nullptr;
    }

    page_map::~page_map() {}

    bool cpu::check_interrupts() {
        return false;
    }

    void cpu::nointerrupts() {}

    void cpu::interrupts() {}

    u32 cpu::current_index() {
        return 0;
    }

    void cpu::request_local_reschedule() {}

    void cpu::relax() noexcept {}

    bool page_map::map_4k(vaddr_t virt, paddr_t, page_flags) {
        ++fake_map_calls;

        fake_last_map_va = virt;

        return fake_page_map_ready && fake_map_calls < fake_map_fail_on_call;
    }

    bool page_map::unmap(vaddr_t) {
        ++fake_unmap_count;

        return fake_page_map_ready;
    }
} // namespace astia::arch

ASTIA_TEST(pci_enumeration_discovers_bar_geometry) {
    astia::hw::driver_handle capture_handle{};

    ASTIA_CHECK(astia::hw::drivers().register_driver(
                    capture, capture_handle, astia::hw::kernel_owner,
                    astia::hw::driver_resource_mmio) == 0);

    astia::u32 enumerated = 0;

    ASTIA_CHECK(astia_test::run_task(astia::hw::pci_enumerate(), enumerated));
    ASTIA_CHECK(enumerated == 1);

    const auto& first = capture.captured.bars[0];

    ASTIA_CHECK(first.address == 0x80000000ull);
    ASTIA_CHECK(first.size == 0x1000ull);
    ASTIA_CHECK(!first.io);
    ASTIA_CHECK(first.address_bits == 32);

    const auto& io = capture.captured.bars[1];

    std::fprintf(stderr, "bar sizes: %llx %llx %llx\n",
                 static_cast<unsigned long long>(first.size),
                 static_cast<unsigned long long>(io.size),
                 static_cast<unsigned long long>(capture.captured.bars[2].size));
    ASTIA_CHECK(io.address == 0xc000ull);
    ASTIA_CHECK(io.size == 0x20ull);
    ASTIA_CHECK(io.io);

    const auto& wide = capture.captured.bars[2];

    ASTIA_CHECK(wide.address == 0x100000000ull);
    ASTIA_CHECK(wide.size == 0x1000ull);
    ASTIA_CHECK(wide.address_bits == 64);
}

ASTIA_TEST(mmio_mapping_rejects_unavailable_kernel_paging) {
    auto invalid = astia::dev::mmio_mapping::map(0, 0);

    ASTIA_CHECK(!invalid);
    ASTIA_CHECK(invalid.error == astia::error_t::einval);

    auto unavailable = astia::dev::mmio_mapping::map(0x1000, 4);

    ASTIA_CHECK(!unavailable);
    ASTIA_CHECK(unavailable.error == astia::error_t::enosys);
}

ASTIA_TEST(pci_bar_public_map_routes_through_pci_mapper) {
    reset_fake_owners();

    astia::hw::pci_bar bar{};

    bar.address = 0x1'0000'0000ull;
    bar.size    = 0x1000;
    bar.present = true;

    auto mapping = bar.map();

    ASTIA_CHECK(mapping);

    if (mapping) {
        ASTIA_CHECK(mapping.value.valid());
        ASTIA_CHECK(mapping.value.base() == reinterpret_cast<volatile void*>(
                                                astia::arch::kernel_base + bar.address));
        mapping.value.release();
    }

    ASTIA_CHECK(fake_pci_map_calls == 1);
    ASTIA_CHECK(fake_pci_unmap_calls == 1);
    ASTIA_CHECK(fake_map_calls == 0);
    ASTIA_CHECK(fake_unmap_count == 0);
    ASTIA_CHECK(fake_reserve_calls == 0);
    ASTIA_CHECK(fake_release_calls == 0);
}

ASTIA_TEST(pci_bar_public_map_malformed_va_rolls_back) {
    reset_fake_owners();

    fake_pci_malformed_va = true;

    astia::hw::pci_bar bar{};

    bar.address = 0x1'0000'0000ull;
    bar.size    = 0x1000;
    bar.present = true;

    auto mapping = bar.map();

    ASTIA_CHECK(!mapping);
    ASTIA_CHECK(mapping.error == astia::error_t::erange);
    ASTIA_CHECK(fake_pci_map_calls == 1);
    ASTIA_CHECK(fake_pci_unmap_calls == 1);
    ASTIA_CHECK(fake_pci_unmap_va == astia::arch::kernel_base + bar.address);
}

ASTIA_TEST(pci_bar_public_map_expected_va_overflow_rejected) {
    reset_fake_owners();

    astia::hw::pci_bar bar{};

    bar.address = 0x1'0000'0000'0000ull;
    bar.size    = 0x1000;
    bar.present = true;

    auto mapping = bar.map();

    ASTIA_CHECK(!mapping);
    ASTIA_CHECK(mapping.error == astia::error_t::erange);
    ASTIA_CHECK(fake_pci_map_calls == 0);
}

ASTIA_TEST(pci_bar_public_map_below_floor_is_busy) {
    reset_fake_owners();

    fake_pci_map_result = false;

    astia::hw::pci_bar bar{};

    bar.address = 0x1000;
    bar.size    = 0x1000;
    bar.present = true;

    auto mapping = bar.map();

    ASTIA_CHECK(!mapping);
    ASTIA_CHECK(mapping.error == astia::error_t::ebusy);
    ASTIA_CHECK(fake_pci_map_calls == 1);
    ASTIA_CHECK(fake_map_calls == 0);
    ASTIA_CHECK(fake_reserve_calls == 0);
}

ASTIA_TEST(pci_bar_public_map_high_address_is_busy) {
    reset_fake_owners();

    fake_pci_map_result = false;

    astia::hw::pci_bar bar{};

    bar.address = 0x8000000000ull;
    bar.size    = 0x1000;
    bar.present = true;

    auto mapping = bar.map();

    ASTIA_CHECK(!mapping);
    ASTIA_CHECK(mapping.error == astia::error_t::ebusy);
    ASTIA_CHECK(fake_pci_map_calls == 1);
    ASTIA_CHECK(fake_map_calls == 0);
    ASTIA_CHECK(fake_reserve_calls == 0);
}

ASTIA_TEST(mmio_mapping_aliased_conflict_is_busy) {
    reset_fake_owners();

    fake_borrow_status  = astia::arch::mmio_borrow_status::aliased;
    fake_page_map_ready = true;

    auto mapping = astia::dev::mmio_mapping::map(0x1'0000'0000ull, 0x1000);

    ASTIA_CHECK(!mapping);
    ASTIA_CHECK(mapping.error == astia::error_t::ebusy);
    ASTIA_CHECK(fake_map_calls == 0);
    ASTIA_CHECK(fake_reserve_calls == 0);
}

ASTIA_TEST(mmio_mapping_out_of_range_is_busy) {
    reset_fake_owners();

    fake_borrow_status  = astia::arch::mmio_borrow_status::out_of_range;
    fake_page_map_ready = true;

    auto mapping = astia::dev::mmio_mapping::map(0x1'0000'0000'0000ull, 0x2000);

    ASTIA_CHECK(!mapping);
    ASTIA_CHECK(mapping.error == astia::error_t::ebusy);
    ASTIA_CHECK(fake_map_calls == 0);
    ASTIA_CHECK(fake_reserve_calls == 0);
}

ASTIA_TEST(mmio_mapping_generic_success_and_release) {
    reset_fake_owners();

    fake_page_map_ready = true;

    auto mapping = astia::dev::mmio_mapping::map(0x1'0000'0000ull, 0x1000);

    ASTIA_CHECK(mapping);

    if (mapping) {
        ASTIA_CHECK(mapping.value.valid());
        mapping.value.release();
    }

    ASTIA_CHECK(fake_map_calls == 1);
    ASTIA_CHECK(fake_unmap_count == 1);
    ASTIA_CHECK(fake_reserve_calls == 1);
    ASTIA_CHECK(fake_release_calls == 1);

    for (const auto& owner : fake_owners) {
        ASTIA_CHECK(!owner.used);
    }
}

ASTIA_TEST(mmio_mapping_borrow_release_has_no_reservation_side_effect) {
    reset_fake_owners();

    fake_borrow_status  = astia::arch::mmio_borrow_status::borrowable;
    fake_page_map_ready = true;

    auto mapping = astia::dev::mmio_mapping::map(0x1'0000'0000ull, 0x1000);

    ASTIA_CHECK(mapping);

    if (mapping) {
        ASTIA_CHECK(mapping.value.valid());
        mapping.value.release();
    }

    ASTIA_CHECK(fake_map_calls == 0);
    ASTIA_CHECK(fake_unmap_count == 0);
    ASTIA_CHECK(fake_release_calls == 0);
    ASTIA_CHECK(fake_invalid_release == 0);
}

ASTIA_TEST(mmio_mapping_pci_overlap_is_busy) {
    reset_fake_owners();

    fake_owners[0] = {
        0x1'0000'0000ull, 0x1'0000'0000ull, 0xffffc00000001000ull, 0x1000, true, true};
    fake_page_map_ready = true;

    auto mapping = astia::dev::mmio_mapping::map(0x1'0000'0000ull, 0x1000);

    ASTIA_CHECK(!mapping);
    ASTIA_CHECK(mapping.error == astia::error_t::ebusy);
    ASTIA_CHECK(fake_map_calls == 0);
    ASTIA_CHECK(fake_reserve_calls == 0);
}

ASTIA_TEST(mmio_mapping_partial_failure_rolls_back) {
    reset_fake_owners();

    fake_page_map_ready   = true;
    fake_map_fail_on_call = 2;

    auto mapping = astia::dev::mmio_mapping::map(0x1'0000'0000ull, 0x2000);

    ASTIA_CHECK(!mapping);
    ASTIA_CHECK(mapping.error == astia::error_t::enomem);
    ASTIA_CHECK(fake_map_calls == 2);
    ASTIA_CHECK(fake_unmap_count == 1);
    ASTIA_CHECK(fake_reserve_calls == 1);
    ASTIA_CHECK(fake_release_calls == 1);

    for (const auto& owner : fake_owners) {
        ASTIA_CHECK(!owner.used);
    }
}

ASTIA_TEST(mmio_mapping_repeated_failure_reuses_window_cursor) {
    reset_fake_owners();

    fake_page_map_ready   = true;
    fake_map_fail_on_call = 1;

    auto first = astia::dev::mmio_mapping::map(0x1'0000'0000ull, 0x1000);

    ASTIA_CHECK(!first);

    const astia::u64 first_va = fake_last_map_va;

    auto second = astia::dev::mmio_mapping::map(0x1'0000'0000ull, 0x1000);

    ASTIA_CHECK(!second);
    ASTIA_CHECK(fake_last_map_va == first_va);
    ASTIA_CHECK(fake_release_calls == 2);

    for (const auto& owner : fake_owners) {
        ASTIA_CHECK(!owner.used);
    }
}

ASTIA_TEST(mmio_mapping_release_preserves_pci_owner) {
    reset_fake_owners();

    fake_page_map_ready = true;

    fake_owners[0] = {0x1000, 0x1000, 0xffffc00000000000ull, 0x1000, true, true};

    auto first = astia::dev::mmio_mapping::map(0x1'0000'0000ull, 0x1000);

    ASTIA_CHECK(first);

    if (first)
        first.value.release();

    ASTIA_CHECK(fake_owners[0].used);
    ASTIA_CHECK(fake_owners[0].pci);

    auto second = astia::dev::mmio_mapping::map(0x1'0000'0000ull, 0x1000);

    ASTIA_CHECK(second);

    fake_owners[0] = {};

    if (second)
        second.value.release();

    for (const auto& owner : fake_owners) {
        ASTIA_CHECK(!owner.used);
    }
}

ASTIA_TEST(mmio_mapping_page_sharing_conflict_is_busy) {
    reset_fake_owners();

    fake_page_map_ready = true;

    auto first = astia::dev::mmio_mapping::map(0x1'0000'0000ull, 0x1000);

    ASTIA_CHECK(first);

    auto second = astia::dev::mmio_mapping::map(0x1'0000'0000ull, 0x1000);

    ASTIA_CHECK(!second);
    ASTIA_CHECK(second.error == astia::error_t::ebusy);
    ASTIA_CHECK(fake_map_calls == 1);
    ASTIA_CHECK(fake_reserve_calls == 1);

    if (first)
        first.value.release();

    ASTIA_CHECK(fake_release_calls == 1);
}

ASTIA_TEST(pci_resource_rollback_supports_reprobe) {
    astia::hw::pci_device device{};

    device.bdf             = {0, 1, 0};
    device.bar_count       = 1;
    device.bars[0].present = true;
    device.bars[0].size    = 0x1000;
    device.bars[0].address = 0x90000000;

    ASTIA_CHECK(astia::hw::pci_set_mmio_mapper(map_mmio, unmap_mmio));
    ASTIA_CHECK(astia::hw::pci_enable_bus_master(device));
    ASTIA_CHECK((device.command & 4u) != 0);
    ASTIA_CHECK(astia::hw::pci_map_bar(device, 0));
    ASTIA_CHECK(!astia::hw::pci_map_bar(device, 0));
    ASTIA_CHECK(device.bars[0].mapped_valid);
    ASTIA_CHECK(astia::hw::pci_unmap_bar(device, 0));
    ASTIA_CHECK(!device.bars[0].mapped_valid);
    ASTIA_CHECK(astia::hw::pci_map_bar(device, 0));
    ASTIA_CHECK(astia::hw::pci_unmap_bar(device, 0));
    ASTIA_CHECK(astia::hw::pci_disable_bus_master(device));
    ASTIA_CHECK((device.command & 4u) == 0);
    ASTIA_CHECK(mmio_maps == 2);
    ASTIA_CHECK(mmio_unmaps == 2);
}
