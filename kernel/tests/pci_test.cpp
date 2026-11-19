#include <astia/dev/driver.hpp>
#include <astia/dev/mmio.hpp>
#include <astia/hw/pci.hpp>
#include "test.hpp"
#include <astia/arch/cpu.hpp>
#include <astia/arch/mmio.hpp>
#include <astia/arch/page_map.hpp>
#include <astia/arch/pci_config.hpp>
#include <cstdio>

namespace {

    struct fake_pci {
        bool probe_low  = false;
        bool probe_high = false;

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
        }
    };

    fake_pci state;

    class capture_driver final : public astia::hw::driver {
        public:
        const char* name() const override {
            return "capture";
        }
        bool match(const astia::hw::device_info&) const override {
            return true;
        }
        bool probe(const astia::hw::device_info& value) override {
            const auto* pci = astia::hw::pci_device_info::get(value);
            if (pci != nullptr)
                captured = pci->device();
            return pci != nullptr;
        }

        astia::hw::pci_device captured{};
    };

    capture_driver capture;

} // namespace

namespace astia::arch {

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

    page_map* kernel_page_map() {
        return nullptr;
    }

    bool cpu::check_interrupts() {
        return false;
    }
    void cpu::nointerrupts() {}
    void cpu::interrupts() {}

    bool page_map::map_4k(vaddr_t, paddr_t, page_flags) {
        return false;
    }
    bool page_map::unmap(vaddr_t) {
        return false;
    }

} // namespace astia::arch

ASTIA_TEST(pci_enumeration_discovers_bar_geometry) {
    ASTIA_CHECK(astia::hw::drivers().register_driver(capture));
    ASTIA_CHECK(astia::hw::pci_enumerate() == 1);

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
