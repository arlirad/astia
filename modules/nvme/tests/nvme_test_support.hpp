#pragma once

#include <astia/mm/dma_buffer.hpp>
#include <astia/module.hpp>

#include <cassert>
#include <cstdlib>

namespace nvme_test {
    inline unsigned    dma_release_count = 0;
    inline const void* released_dma[64]{};
    inline unsigned    released_dma_count  = 0;
    inline bool        host_dma            = false;
    inline astia::u64  bar_mapped          = 0;
    inline astia::u64  bar_size            = 0x4000;
    inline unsigned    bus_master_enables  = 0;
    inline unsigned    bus_master_disables = 0;
    inline unsigned    bar_unmaps          = 0;
    inline unsigned    msix_disables       = 0;
    inline bool        map_bar_result      = true;
    inline bool        unmap_bar_result    = true;
    inline bool        disable_msix_result = true;
}

namespace astia::mm {
    dma_buffer::dma_buffer(dma_buffer&&) noexcept {}

    dma_buffer& dma_buffer::operator=(dma_buffer&&) noexcept {
        return *this;
    }

    dma_buffer::~dma_buffer() {
        release();
    }

    bool dma_buffer::allocate(usz pages, bool zero) {
        if (!nvme_test::host_dma || pages == 0)
            return false;

        const usz size = pages * astia::mm::page_size;

        auto* memory =
            static_cast<unsigned char*>(std::aligned_alloc(astia::mm::page_size, size));
        if (memory == nullptr)
            return false;

        if (zero) {
            for (usz i = 0; i < size; ++i) {
                memory[i] = 0;
            }
        }

        _physical = reinterpret_cast<astia::u64>(memory);
        _address  = memory;
        _pages    = pages;

        return true;
    }

    void dma_buffer::release() {
        if (_address != nullptr) {
            std::free(_address);

            _physical = 0;
            _address  = nullptr;
            _pages    = 0;
        }

        for (unsigned i = 0; i < nvme_test::released_dma_count; ++i) {
            if (nvme_test::released_dma[i] == this)
                return;
        }

        assert(nvme_test::released_dma_count <
               sizeof nvme_test::released_dma / sizeof nvme_test::released_dma[0]);

        nvme_test::released_dma[nvme_test::released_dma_count++] = this;

        ++nvme_test::dma_release_count;
    }

    void dma_buffer::swap(dma_buffer&) noexcept {}
}

namespace astia::module {
    msix_route::~msix_route() {}

    bool pci_enable_bus_master(hw::pci_device& device) {
        device.bus_master_enabled = true;

        ++nvme_test::bus_master_enables;

        return true;
    }

    bool pci_disable_bus_master(hw::pci_device& device) {
        device.bus_master_enabled = false;

        ++nvme_test::bus_master_disables;

        return true;
    }

    bool pci_disable_msix(hw::pci_device&) {
        ++nvme_test::msix_disables;

        return nvme_test::disable_msix_result;
    }

    bool pci_map_bar(hw::pci_device& device, u8 index) {
        if (!nvme_test::map_bar_result)
            return false;

        device.bars[index].mapped_valid = true;

        return true;
    }

    bool pci_unmap_bar(hw::pci_device& device, u8 index) {
        ++nvme_test::bar_unmaps;

        device.bars[index].mapped_valid = false;

        return nvme_test::unmap_bar_result;
    }

    u64 pci_bar_mapped(const hw::pci_device&, u8) {
        return nvme_test::bar_mapped;
    }

    u64 pci_bar_size(const hw::pci_device&, u8) {
        return nvme_test::bar_size;
    }

    bool pci_msix_request(hw::pci_device*, arch::interrupt_vector_handler, void*,
                          msix_route*) {
        return false;
    }

    bool pci_msix_program(msix_route*) {
        return false;
    }

    bool pci_msix_enable(msix_route*) {
        return false;
    }

    bool pci_msix_unmask(msix_route*) {
        return true;
    }

    bool pci_msix_disable(msix_route*) {
        return true;
    }

    bool pci_msix_release(msix_route*) {
        return true;
    }

    void log(const char*, const char*) {}

    void log_u64(const char*, const char*, u64) {}
}
