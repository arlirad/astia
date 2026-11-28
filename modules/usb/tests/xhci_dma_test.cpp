#include "xhci_test_support.hpp"

#define ASTIA_USB_TEST_MMIO xhci_test::test_mmio
#include "../xhci.cpp"

#include <cassert>

using namespace astia::usb::xhci;

namespace {
    astia::hw::pci_device make_device() {
        astia::hw::pci_device device{};

        device.class_code            = 0x0c;
        device.subclass              = 0x03;
        device.programming_interface = 0x30;
        device.bar_count             = 1;
        device.bars[0].present       = true;

        return device;
    }

    void prepare(bool address_64) {
        xhci_test::reset_allocator();
        xhci_test::reset_bar(address_64);
    }

    void assert_low_allocations() {
        assert(xhci_test::allocation_count == 7);

        for (unsigned i = 0; i < xhci_test::allocation_count; ++i) {
            const astia::u64 physical = xhci_test::allocation_physical[i];
            const astia::u64 size = xhci_test::allocation_pages[i] * astia::mm::page_size;

            assert(physical < xhci_test::dma_32bit_limit);
            assert(physical + size <= xhci_test::dma_32bit_limit);
        }
    }

    void assert_high_allocations() {
        assert(xhci_test::allocation_count == 7);

        for (unsigned i = 0; i < xhci_test::allocation_count; ++i) {
            const astia::u64 physical = xhci_test::allocation_physical[i];
            const astia::u64 size = xhci_test::allocation_pages[i] * astia::mm::page_size;

            assert(physical >= xhci_test::dma_32bit_limit);
            assert(physical + size > xhci_test::dma_32bit_limit);
        }
    }

    void assert_low_programming() {
        const astia::u64 dcbaap = xhci_test::load64(xhci_test::cap_length + 0x30);
        const astia::u64 crcr   = xhci_test::load64(xhci_test::cap_length + 0x18);
        const astia::u64 erstba = xhci_test::load64(xhci_test::runtime_base + 0x10);
        const astia::u64 erdp   = xhci_test::load64(xhci_test::runtime_base + 0x18);

        assert(dcbaap != 0 && dcbaap < xhci_test::dma_32bit_limit);
        assert((crcr & ~astia::u64{1}) < xhci_test::dma_32bit_limit);
        assert(erstba != 0 && erstba < xhci_test::dma_32bit_limit);
        assert(erdp != 0 && erdp < xhci_test::dma_32bit_limit);
    }

    void assert_high_programming() {
        const astia::u64 dcbaap = xhci_test::load64(xhci_test::cap_length + 0x30);
        const astia::u64 crcr   = xhci_test::load64(xhci_test::cap_length + 0x18);
        const astia::u64 erstba = xhci_test::load64(xhci_test::runtime_base + 0x10);
        const astia::u64 erdp   = xhci_test::load64(xhci_test::runtime_base + 0x18);

        assert(dcbaap >= xhci_test::dma_32bit_limit);
        assert((crcr & ~astia::u64{1}) >= xhci_test::dma_32bit_limit);
        assert(erstba >= xhci_test::dma_32bit_limit);
        assert(erdp >= xhci_test::dma_32bit_limit);
    }

    void test_probe_reads_address_64() {
        prepare(false);

        probe_result result{};

        assert(probe(xhci_test::bar, xhci_test::bar_size, result));
        assert(result.valid);
        assert(!result.capabilities.address_64);

        prepare(true);

        assert(probe(xhci_test::bar, xhci_test::bar_size, result));
        assert(result.valid);
        assert(result.capabilities.address_64);
    }

    void test_ac64_clear_uses_low_dma() {
        prepare(false);

        astia::hw::pci_device device = make_device();
        controller            value{};

        assert(value.start(&device) == 0);
        assert_low_allocations();
        assert_low_programming();
        assert(xhci_test::dma_programming_writes != 0);
        assert(value.stop());
    }

    void test_ac64_clear_high_only_fails_without_programming() {
        prepare(false);

        xhci_test::low_memory_available = false;

        astia::hw::pci_device device = make_device();
        controller            value{};

        assert(value.start(&device) != 0);
        assert(xhci_test::allocation_count == 0);
        assert(xhci_test::dma_programming_writes == 0);
        assert(!device.bars[0].mapped_valid);
        assert(!device.bus_master_enabled);
        assert(xhci_test::unmap_count == 1);
        assert(xhci_test::bus_master_disables == 1);
    }

    void test_ac64_set_keeps_unlimited_dma() {
        prepare(true);

        xhci_test::next_high_physical = xhci_test::dma_32bit_limit;

        astia::hw::pci_device device = make_device();
        controller            value{};

        assert(value.start(&device) == 0);
        assert_high_allocations();
        assert_high_programming();
        assert(xhci_test::dma_programming_writes != 0);
        assert(value.stop());
    }
} // namespace

int main() {
    test_probe_reads_address_64();
    test_ac64_clear_uses_low_dma();
    test_ac64_clear_high_only_fails_without_programming();
    test_ac64_set_keeps_unlimited_dma();

    return 0;
}
